#pragma once

#include "bytecode.hpp"
#include "escape.hpp"
#include "filters.hpp"
#include "serialize_value.hpp"
#include "resolve.hpp"
#include "../injamm.hpp"
#include <array>
#include <charconv>
#include <cmath>
#include <expected>
#include <string>

namespace injamm::detail {

/**
 * @brief ループ状態を保持する構造体
 * @details セクション内の反復処理で使用する。現在のインデックス、総要素数、
 *          親ループへのポインタを持つ。これによりネストしたループにおける
 *          @index / @first / @last の解決が可能になる。
 */
struct bc_loop_state {
  std::uint32_t index = 0;
  /**< 現在のループインデックス（0始まり） */
  std::uint32_t count = 0;
  /**< ループ内の総要素数 */
  std::string_view key{};
  /**< 現在の要素のキー名（@key 用、マップ反復時のみ設定） */
  bc_loop_state const* parent = nullptr;
  /**< 親ループ状態へのポインタ。ネスト時のみ使用 */
  mutable bool break_flag = false;
  /**< break 要求フラグ（子 executor からセット） */
  mutable bool continue_flag = false;
  /**< continue 要求フラグ（子 executor からセット） */
};

/**
 * @brief バイトコード VM の実行コンテキスト
 * @tparam T コンテキスト値の型
 * @tparam RootT ルートコンテキスト値の型（デフォルトは T）
 * @details Glaze リフレクションを用いた O(1) フィールドアクセスと
 *           computed goto（GCC）による高速ディスパッチを特徴とする。
 *          Mustache/inja サブセットの全命令を実行する。
 */
template <class T, class RootT = T>
class bc_executor {
  bytecode const& bc_;
  T const& value_;
  RootT const& root_value_;
  bc_loop_state const* loop_ = nullptr;
  std::string& out_;

  /**
   * @brief ネストされたドット区切りパスを再帰的に解決し visitor を呼び出す
   * @tparam V 現在のフィールドの型
   * @tparam F visitor の型
   * @param v 現在の値
   * @param path ドット区切りのパス文字列（例: "founder.address.city"）
   * @param visitor 各フィールドに対して呼ばれるコールバック
   * @return std::expected<void, error_ctx> エラー発生時に unexpected を返す
   * @details パスを最初のキーと残りのパスに分割し、最初のキーでフィールドを検索してから
   *          残りのパスで再帰する。Glaze のコンパイル時リフレクションにより
   *          フィールド名の比較を展開する。
   */
  template <class V, class F>
  auto resolve_nested_path(V const& v, std::string_view path, F&& visitor) const
      -> std::expected<void, error_ctx> {
    /** ドットの位置で分割: 末端に達したか判定 */
    auto dot_pos = path.find('.');
    if (dot_pos == std::string_view::npos) {
      /** 末端: パス全体をキーとして一致するフィールドを探し visitor を適用 */
      if constexpr (ct_glz_reflectable<V>) {
        constexpr auto sz = static_cast<std::size_t>(glz::reflect<V>::size);
        auto tied = glz::to_tie(v);
        using visitor_r = decltype(visitor(glz::get<0>(tied)));
        if constexpr (std::same_as<visitor_r, void>) {
          /** visitor が void を返す場合: fold 式で全フィールドを走査 */
          [&]<std::size_t... I>(std::index_sequence<I...>) {
            (([&] {
              if (std::string_view{glz::reflect<V>::keys[I]} == path) {
                visitor(glz::get<I>(tied));
              }
            }()), ...);
          }(std::make_index_sequence<sz>{});
        } else {
          /** visitor が std::expected を返す場合: エラーを伝搬する */
          std::expected<void, error_ctx> result{};
          [&]<std::size_t... I>(std::index_sequence<I...>) {
            (([&] {
              if (!result) return;
              if (std::string_view{glz::reflect<V>::keys[I]} == path) {
                result = visitor(glz::get<I>(tied));
              }
            }()), ...);
          }(std::make_index_sequence<sz>{});
          return result;
        }
      }
      return {};
    }

    /** 中間ノード: 最初のキーを取得し、残りのパスで再帰 */
    auto first_key = path.substr(0, dot_pos);
    auto rest_path = path.substr(dot_pos + 1);

    if constexpr (ct_glz_reflectable<V>) {
      constexpr auto sz = static_cast<std::size_t>(glz::reflect<V>::size);
      auto tied = glz::to_tie(v);
      using visitor_r = decltype(visitor(glz::get<0>(tied)));
      if constexpr (std::same_as<visitor_r, void>) {
        /** visitor が void の場合: フィールドを発見次第再帰 */
        [&]<std::size_t... I>(std::index_sequence<I...>) {
          (([&] {
            if (std::string_view{glz::reflect<V>::keys[I]} == first_key) {
              auto const& field = glz::get<I>(tied);
              using FT = std::remove_cvref_t<decltype(field)>;
              if constexpr (ct_glz_reflectable<FT>) {
                (void)resolve_nested_path(field, rest_path, std::forward<F>(visitor));
              }
            }
          }()), ...);
        }(std::make_index_sequence<sz>{});
      } else {
        /** visitor が expected を返す場合: エラーを伝搬しながら再帰 */
        std::expected<void, error_ctx> result{};
        [&]<std::size_t... I>(std::index_sequence<I...>) {
          (([&] {
            if (!result) return;
            if (std::string_view{glz::reflect<V>::keys[I]} == first_key) {
              auto const& field = glz::get<I>(tied);
              using FT = std::remove_cvref_t<decltype(field)>;
              if constexpr (ct_glz_reflectable<FT>) {
                result = resolve_nested_path(field, rest_path, std::forward<F>(visitor));
              }
            }
          }()), ...);
        }(std::make_index_sequence<sz>{});
        return result;
      }
    }
    return {};
  }

  /**
   * @brief 指定されたキーでフィールドを検索し visitor を呼び出す
   * @tparam V 検索対象の型
   * @tparam F visitor の型
   * @param v 検索対象の値
   * @param key フィールド名またはドット区切りパス
   * @param field_index プリコンパイルされたフィールドインデックス（不明なら UINT32_MAX）
   * @param visitor 発見時に呼ばれるコールバック
   * @return std::expected<void, error_ctx> エラー伝搬用
   * @details field_index が有効なら O(1) アクセス、そうでなければ線形探索を行う。
   *          キーにドットが含まれる場合は resolve_nested_path に委譲する。
   */
  template <class V, class F>
  auto for_each_field(V const& v, std::string_view key, std::uint32_t field_index, F&& visitor) const
      -> std::expected<void, error_ctx> {
    /** ネストパスが含まれている場合は再帰解決に委譲 */
    if (key.find('.') != std::string_view::npos) {
      return resolve_nested_path(v, key, std::forward<F>(visitor));
    }

    if constexpr (ct_glz_reflectable<V>) {
      constexpr auto sz = static_cast<std::size_t>(glz::reflect<V>::size);
      auto tied = glz::to_tie(v);
      using visitor_t = decltype(visitor(glz::get<0>(tied)));

      /**
       * O(1) アクセス: field_index が有効な場合
       * if-else チェーンで実行時の field_index 値をコンパイル時定数に変換し、
       * 該当フィールドに直接アクセスする。
       */
      if (field_index != UINT32_MAX && field_index < sz) {
        auto visit_by_index = [&]<std::size_t... I>(std::index_sequence<I...>) -> std::expected<void, error_ctx> {
          std::expected<void, error_ctx> visitor_result{};
          /** 単一インデックスを試行する内部ラムダ。見つかれば visitor を適用。 */
          auto try_index = [&]<std::size_t Idx>() -> bool {
            if (field_index == Idx) {
              if constexpr (std::same_as<visitor_t, void>) {
                visitor(glz::get<Idx>(tied));
              } else {
                visitor_result = visitor(glz::get<Idx>(tied));
              }
              return true;
            }
            return false;
          };
          /** fold 式で全インデックスを試行（最初の一致で短絡） */
          bool found = (try_index.template operator()<I>() || ...);
          (void)found;
          return visitor_result;
        };
        return visit_by_index(std::make_index_sequence<sz>{});
      }

      /**
       * フォールバック: 線形探索
       * フィールドインデックスが不明な場合、全てのフィールドを走査して
       * キー名と一致するものを探す。
       */
      if constexpr (std::same_as<visitor_t, void>) {
        [&]<std::size_t... I>(std::index_sequence<I...>) {
          (([&] {
            if (std::string_view{glz::reflect<V>::keys[I]} == key) {
              visitor(glz::get<I>(tied));
            }
          }()), ...);
        }(std::make_index_sequence<sz>{});
        return {};
      } else {
        std::expected<void, error_ctx> result{};
        [&]<std::size_t... I>(std::index_sequence<I...>) {
          (([&] {
            if (!result) return;
            if (std::string_view{glz::reflect<V>::keys[I]} == key) {
              result = visitor(glz::get<I>(tied));
            }
          }()), ...);
        }(std::make_index_sequence<sz>{});
        return result;
      }
    }
    return {};
  }

  /**
   * @brief フィールドの値を出力バッファに追記する
   * @param field 出力対象のフィールド
   * @param raw HTMLエスケープを行わない場合は true
   * @details 型に応じて適切なシリアライズを行う:
   *          - bool → "true" / "false"
   *          - std::string / std::string_view → raw フラグに応じてエスケープ有無
   *          - 算術型 → std::to_chars による高速変換
   */
  void emit_var_value(auto const& field, bool raw) {
    using FT = std::remove_cvref_t<decltype(field)>;
    if constexpr (std::same_as<FT, bool>) {
      if (field) {
        out_.append("true");
      } else {
        out_.append("false");
      }
    } else if constexpr (std::same_as<FT, std::string> || std::same_as<FT, std::string_view>) {
      if (raw) {
        out_.append(field);
      } else {
        html_escape_into(out_, field);
      }
    } else if constexpr (std::is_arithmetic_v<FT> && !std::same_as<FT, bool>) {
      std::array<char, 32> buf;
      auto [ptr, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), field);
      if (ec == std::errc{}) {
        out_.append(buf.data(), ptr);
      }
    } else if constexpr (is_std_optional_v<FT>) {
      if (field.has_value()) {
        emit_var_value(*field, raw);
      }
    }
  }

public:
  bc_executor(bytecode const& bc, T const& value, RootT const& root_value,
              bc_loop_state const* loop, std::string& out)
      : bc_(bc), value_(value), root_value_(root_value), loop_(loop), out_(out) {}

  /**
   * @brief バイトコードの実行を開始する
   * @return std::expected<void, error_ctx> 実行結果
   */
  std::expected<void, error_ctx> execute() {
    return execute_impl(0, bc_.instructions.size());
  }

  /**
   * @brief バイトコードを指定範囲で実行する（内部実装）
   * @param start 開始インデックス
   * @param end 終了インデックス（排他）
   * @return std::expected<void, error_ctx> 実行結果
   * @details GCC の computed goto（threaded code）を用いた高速ディスパッチと、
   *          汎用性のための switch ベースのフォールバックを #if で切り替える。
   *          各命令ハンドラは L_emit_* ラベルとして実装され、DISPATCH マクロで
   *          次の命令にジャンプする。
   */
  std::expected<void, error_ctx> execute_impl(std::size_t start, std::size_t end) {
    std::size_t pc = start;
    std::string filtered_value_;

#if defined(__GNUC__) && !defined(__clang__) && defined(INJAMM_THREADED_DISPATCH)
    /**
     * GCC computed goto（threaded code dispatch）
     * 各バイトコードにラベルアドレスを割り当て、DISPATCH マクロで直接ジャンプする。
     * switch 文と比較して分岐予測の精度が向上し実行が高速化する。
     * 対応: GCC のみ（Clang は computed goto をサポートしない）
     */
    static void* dispatch_table[] = {
      &&L_emit_literal,      // 0
      &&L_emit_var,          // 1
      &&L_emit_var_raw,      // 2
      &&L_emit_section,      // 3
      &&L_emit_section_bool, // 4
      &&L_emit_end,          // 5
      &&L_emit_inverted,     // 6
      &&L_emit_at_index,     // 7
      &&L_emit_at_first,     // 8
      &&L_emit_at_last,      // 9
      &&L_emit_if,           // 10
      &&L_emit_else,         // 11
      &&L_emit_endif,        // 12
      &&L_emit_at_section,   // 13
      &&L_emit_at_inverted,  // 14
      &&L_emit_litvar,       // 15
      &&L_emit_litvar_raw,   // 16
      &&L_emit_at_root,      // 17
      &&L_emit_at_root_field,    // 18
      &&L_emit_at_root_field_raw, // 19
      &&L_emit_at_key,       // 20
      &&L_emit_this,         // 21
      &&L_resolve_filtered,  // 22
      &&L_filter_upper,      // 23
      &&L_filter_lower,      // 24
      &&L_filter_capitalize, // 25
      &&L_filter_title,      // 26
      &&L_filter_trim,       // 27
      &&L_filter_ltrim,      // 28
      &&L_filter_rtrim,      // 29
      &&L_filter_left,       // 30
      &&L_filter_right,      // 31
      &&L_filter_center,     // 32
      &&L_filter_truncate,   // 33
      &&L_filter_substr,     // 34
      &&L_emit_filtered,     // 35
      &&L_emit_filtered_raw, // 36
      &&L_filter_int_abs,    // 37
      &&L_filter_int_hex,    // 38
      &&L_filter_int_oct,    // 39
      &&L_filter_int_bin,    // 40
      &&L_filter_int_neg,    // 41
      &&L_filter_int_mod,    // 42
      &&L_filter_int_numify, // 43
      &&L_filter_int_is_neg, // 44
      &&L_filter_int_eq,     // 45
      &&L_filter_int_zerofill, // 46
      &&L_filter_float_precision, // 47
      &&L_emit_if_filtered,  // 48
      &&L_emit_break,        // 49
      &&L_emit_continue,     // 50
      &&L_halt,              // 51
    };

/** @brief 現在の命令のオペコードに対応するラベルにジャンプする */
#define DISPATCH() goto *dispatch_table[static_cast<int>(bc_.instructions[pc].op)]

    DISPATCH();

    /** @brief リテラル文字列を出力に追記する */
    L_emit_literal: {
      out_.append(bc_.literals[bc_.instructions[pc].operand]);
      ++pc;
      DISPATCH();
    }

    /** @brief 変数の値を出力する（raw / escaped） */
    L_emit_var:
    L_emit_var_raw: {
      auto const& ref = bc_.var_refs[bc_.instructions[pc].operand];
      bool raw = (bc_.instructions[pc].op == bc_opcode::emit_var_raw);
      auto r = for_each_field(value_, ref.key, ref.field_index, [&](auto const& field) {
        emit_var_value(field, raw);
      });
      if (!r) return r;
      ++pc;
      DISPATCH();
    }

    /**
     * @brief セクションブロックの開始
     * @details 配列の場合はループして各要素を描画、bool の場合は真ならボディを描画。
     *          ループ時は bc_loop_state を生成して子 executor に渡す。
     */
    L_emit_section: {
      auto const& instr = bc_.instructions[pc];
      auto const& ref = bc_.var_refs[instr.operand2];
      auto body_end = instr.operand;

      if (body_end <= pc + 1 || body_end > bc_.instructions.size()) {
        return std::unexpected(error_ctx{.position = pc, .ec = error_code::syntax_error});
      }

      auto r = for_each_field(value_, ref.key, ref.field_index,
          [&](auto const& field) -> std::expected<void, error_ctx> {
        using FT = std::remove_cvref_t<decltype(field)>;
        if constexpr (ct_is_vector_like<FT>) {
          /** 配列の場合: 各要素をループ */
          using elem_t = typename FT::value_type;
          bc_loop_state ls;
          ls.count = static_cast<std::uint32_t>(field.size());

          for (ls.index = 0; ls.index < static_cast<std::uint32_t>(field.size()); ++ls.index) {
            ls.continue_flag = false;
            bc_executor<elem_t, RootT> child_exec(bc_, field[ls.index], root_value_, &ls, out_);
            auto r2 = child_exec.execute_impl(pc + 1, body_end - 1);
            if (!r2) return r2;
            if (ls.continue_flag) {
              ls.continue_flag = false;
              continue;
            }
            if (ls.break_flag) break;
           }
          } else if constexpr (std::same_as<FT, bool>) {
           /** bool の場合: 真ならボディを一度描画 */
           if (field) {
             auto r2 = execute_impl(pc + 1, body_end - 1);
             if (!r2) return r2;
           }
          } else if constexpr (is_std_optional_v<FT>) {
            /** optional の場合: 値を持てば内部値をコンテキストとしてボディを一度描画 */
            if (field.has_value()) {
              using inner_t = typename FT::value_type;
              bc_executor<inner_t, RootT> child_exec(bc_, *field, root_value_, nullptr, out_);
              auto r2 = child_exec.execute_impl(pc + 1, body_end - 1);
              if (!r2) return r2;
            }
          } else if constexpr (ct_is_map_like<FT>) {
            /** map の場合: キーを @key として各要素をループ */
            bc_loop_state ls;
            ls.count = static_cast<std::uint32_t>(field.size());
            for (auto const& [k, v] : field) {
              ls.key = std::string_view{k};
              using val_t = std::remove_cvref_t<decltype(v)>;
              bc_executor<val_t, RootT> child_exec(bc_, v, root_value_, &ls, out_);
              auto r2 = child_exec.execute_impl(pc + 1, body_end - 1);
              if (!r2) return r2;
              if (ls.break_flag) break;
              ++ls.index;
            }
          } else if constexpr (ct_is_set_like<FT>) {
            /** set の場合: 各要素を {{this}} としてイテレータベースでループ */
            using elem_t = typename FT::value_type;
            bc_loop_state ls;
            ls.count = static_cast<std::uint32_t>(field.size());
            for (auto const& elem : field) {
              ls.continue_flag = false;
              bc_executor<elem_t, RootT> child_exec(bc_, elem, root_value_, &ls, out_);
              auto r2 = child_exec.execute_impl(pc + 1, body_end - 1);
              if (!r2) return r2;
              if (ls.continue_flag) {
                ls.continue_flag = false;
                continue;
              }
              if (ls.break_flag) break;
              ++ls.index;
            }
          } else if constexpr (ct_glz_reflectable<FT>) {
           /** 構造体の場合: 全フィールドを反復 */
           constexpr auto sz = glz::reflect<FT>::size;
           auto tied = glz::to_tie(field);
           std::expected<void, error_ctx> res{};
           [&]<std::size_t... I>(std::index_sequence<I...>) {
             (([&] {
               if (!res) return;
               bc_loop_state ls;
               ls.count = sz;
               ls.index = I;
               ls.key = glz::reflect<FT>::keys[I];
               using elem_t = std::remove_cvref_t<decltype(glz::get<I>(tied))>;
               bc_executor<elem_t, RootT> child_exec(bc_, glz::get<I>(tied), root_value_, &ls, out_);
               res = child_exec.execute_impl(pc + 1, body_end - 1);
             }()), ...);
           }(std::make_index_sequence<sz>{});
           return res;
         }
         return {};
       });
      if (!r) return r;
      pc = body_end;
      DISPATCH();
    }

    /**
     * @brief bool 専用セクションブロック（最適化用）
     * @details パーサーが配列ではないことが確定している場合に emit_section の
     *          代わりに発行される。ベクター分岐を省略できる。
     */
    L_emit_section_bool: {
      auto const& instr = bc_.instructions[pc];
      auto const& ref = bc_.var_refs[instr.operand2];
      auto body_end = instr.operand;

      if (body_end <= pc + 1 || body_end > bc_.instructions.size()) {
        return std::unexpected(error_ctx{.position = pc, .ec = error_code::syntax_error});
      }

      auto r = for_each_field(value_, ref.key, ref.field_index,
          [&](auto const& field) -> std::expected<void, error_ctx> {
        using FT = std::remove_cvref_t<decltype(field)>;
        if constexpr (std::same_as<FT, bool>) {
          if (field) {
            auto r2 = execute_impl(pc + 1, body_end - 1);
            if (!r2) return r2;
          }
        }
        return {};
      });
      if (!r) return r;
      pc = body_end;
      DISPATCH();
    }

    /** @brief 実行終端（通常到達しない） */
    L_emit_end: {
      return {};
    }

    /**
     * @brief 逆セクションの開始
     * @details 配列が空または bool が偽の場合にボディを描画する。
     *          条件が成立しなければ operand の位置（endif）にジャンプする。
     */
    L_emit_inverted: {
      auto const& instr = bc_.instructions[pc];
      auto const& ref = bc_.var_refs[instr.operand2];
      bool empty = true;
      (void)for_each_field(value_, ref.key, ref.field_index, [&](auto const& field) {
        using FT = std::remove_cvref_t<decltype(field)>;
        if constexpr (ct_is_vector_like<FT>) {
          empty = field.empty();
        } else if constexpr (std::same_as<FT, bool>) {
          empty = !field;
        } else if constexpr (is_std_optional_v<FT>) {
          empty = !field.has_value();
        } else if constexpr (ct_is_map_like<FT>) {
          empty = field.empty();
        } else if constexpr (ct_is_set_like<FT>) {
          empty = field.empty();
        }
      });
      if (empty) {
        auto body_end = instr.operand;
        if (body_end <= pc + 1 || body_end > bc_.instructions.size()) {
          return std::unexpected(error_ctx{.position = pc, .ec = error_code::syntax_error});
        }
        auto r = execute_impl(pc + 1, body_end - 1);
        if (!r) return r;
        pc = body_end;
      } else {
        pc = instr.operand;
      }
      DISPATCH();
    }

    /** @brief ループの @index を数値として出力する */
    L_emit_at_index: {
      if (loop_) {
        std::array<char, 16> buf;
        auto [ptr, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), loop_->index);
        if (ec == std::errc{}) {
          out_.append(buf.data(), ptr);
        }
      }
      ++pc;
      DISPATCH();
    }

    /** @brief ループ先頭なら "true" を出力する */
    L_emit_at_first: {
      if (loop_ && loop_->index == 0) {
        out_.append("true");
      } else {
        out_.append("false");
      }
      ++pc;
      DISPATCH();
    }

    /** @brief ループ末尾なら "true" を出力する */
    L_emit_at_last: {
      if (loop_ && loop_->index + 1 == loop_->count) {
        out_.append("true");
      } else {
        out_.append("false");
      }
      ++pc;
      DISPATCH();
    }

    /**
     * @brief if 条件分岐
     * @details @last/@first/@index の特殊変数または通常フィールドを評価し、
     *          条件が偽なら operand（else または endif の位置）にジャンプする。
     */
    L_emit_if: {
      auto const& instr = bc_.instructions[pc];
      auto const& ref = bc_.var_refs[instr.operand2];
      bool cond = false;

      /** @last, @first, @index の場合は loop 変数を直接参照 */
      if (ref.key.starts_with("@")) {
        if (loop_) {
          if (ref.key == "@last") {
            cond = (loop_->index + 1 == loop_->count);
          } else if (ref.key == "@first") {
            cond = (loop_->index == 0);
          } else if (ref.key == "@index") {
            /** @index は 0 以外で真（0 は偽扱い） */
            cond = (loop_->index != 0);
          } else if (ref.key == "@key") {
            /** @key は空でなければ真 */
            cond = !loop_->key.empty();
          }
        }
      } else {
        /** 通常のフィールド参照による truthiness 判定 */
        (void)for_each_field(value_, ref.key, ref.field_index, [&](auto const& field) {
          using FT = std::remove_cvref_t<decltype(field)>;
          if constexpr (std::same_as<FT, bool>) {
            cond = field;
          } else if constexpr (ct_is_vector_like<FT>) {
            cond = !field.empty();
          } else if constexpr (std::is_arithmetic_v<FT>) {
            cond = (field != 0);
          } else if constexpr (is_std_optional_v<FT>) {
            cond = field.has_value();
          } else if constexpr (ct_is_map_like<FT>) {
            cond = !field.empty();
          } else if constexpr (ct_is_set_like<FT>) {
            cond = !field.empty();
          }
        });
      }

      if (!cond) {
        /** 条件偽: operand のジャンプ先（else の次 or endif の次）に移動 */
        pc = instr.operand;
      } else {
        /** 条件真: 次の命令（then ブロック）に進む */
        ++pc;
      }
      DISPATCH();
    }

    /** @brief else ブランチ: operand に設定されたジャンプ先に移動する */
    L_emit_else: {
      auto const& instr = bc_.instructions[pc];
      pc = instr.operand;
      DISPATCH();
    }

    /** @brief endif: if ブロック終端、次の命令に進む */
    L_emit_endif: {
      ++pc;
      DISPATCH();
    }

    /** @brief @var セクション（現在未使用、将来拡張用） */
    L_emit_at_section: {
      auto const& instr = bc_.instructions[pc];
      bool cond = false;
      if (loop_) {
        auto kind = instr.operand2;
        if (kind == 0) {
          cond = loop_->index > 0;
        } else if (kind == 1) {
          cond = loop_->index == 0;
        } else if (kind == 2) {
          cond = loop_->index + 1 == loop_->count;
        }
      }
      if (cond) {
        auto body_end = instr.operand;
        auto r = execute_impl(pc + 1, body_end - 1);
        if (!r) return r;
        pc = body_end;
      } else {
        pc = instr.operand;
      }
      DISPATCH();
    }

    /**
     * @brief @var 逆セクション
     * @details @index が 0、または @first/@last が偽のときにボディを描画する。
     *          operand2 で @index(0)/@first(1)/@last(2) を区別する。
     */
    L_emit_at_inverted: {
      auto const& instr = bc_.instructions[pc];
      bool cond = false;
      if (loop_) {
        auto kind = instr.operand2;
        if (kind == 0) {
          cond = loop_->index == 0;
        } else if (kind == 1) {
          /** @first: 先頭要素の場合は逆セクションをスキップ */
          cond = loop_->index == 0;
        } else if (kind == 2) {
          /** @last: 末尾要素の場合は逆セクションをスキップ */
          cond = loop_->index + 1 == loop_->count;
        }
      }
      if (cond) {
        /** 条件成立: ボディをスキップして operand にジャンプ */
        pc = instr.operand;
      } else {
        /** 条件不成立: ボディを描画 */
        auto body_end = instr.operand;
        auto r = execute_impl(pc + 1, body_end - 1);
        if (!r) return r;
        pc = body_end;
      }
      DISPATCH();
    }

    /**
     * @brief 融合命令: リテラル + 変数（最適化）
     * @details パーサーが静的に隣接するリテラルと変数を検出した際に
     *          一命令に統合する。出力バッファへの追記回数を削減できる。
     */
    L_emit_litvar:
    L_emit_litvar_raw: {
      auto const& instr = bc_.instructions[pc];
      /** リテラル部分を出力 */
      out_.append(bc_.literals[instr.operand]);
      /** 変数部分を出力 */
      auto const& ref = bc_.var_refs[instr.operand2];
      bool raw = (instr.op == bc_opcode::emit_litvar_raw);
      auto r = for_each_field(value_, ref.key, ref.field_index, [&](auto const& field) {
        emit_var_value(field, raw);
      });
      if (!r) return r;
      ++pc;
      DISPATCH();
    }

    /** @brief @root: ルートコンテキスト全体をシリアライズして出力する */
    L_emit_at_root: {
      if constexpr (serializable_v<RootT>) {
        serialize_value(out_, root_value_);
      }
      ++pc;
      DISPATCH();
    }

    /** @brief @root.field: ルートコンテキストのフィールドを解決して出力する */
    L_emit_at_root_field:
    L_emit_at_root_field_raw: {
      auto const& ref = bc_.var_refs[bc_.instructions[pc].operand];
      bool raw = (bc_.instructions[pc].op == bc_opcode::emit_at_root_field_raw);
      auto r = for_each_field(root_value_, ref.key, ref.field_index, [&](auto const& field) {
        emit_var_value(field, raw);
      });
      if (!r) return r;
      ++pc;
      DISPATCH();
    }

    /** @brief @key: ループ内の現在要素キー名を出力する */
    L_emit_at_key: {
      if (loop_) {
        if (!loop_->key.empty()) {
          out_.append(loop_->key);
        } else {
          /** キーが空の場合（配列反復など）はインデックスを文字列として出力 */
          std::array<char, 16> buf;
          auto [ptr, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), loop_->index);
          if (ec == std::errc{}) {
            out_.append(buf.data(), ptr);
          }
        }
      }
      ++pc;
      DISPATCH();
    }

    /** @brief {{this}}: 現在のコンテキスト自体を出力する */
    L_emit_this: {
      if constexpr (serializable_v<T>) {
        serialize_value(out_, value_);
      } else if constexpr (ct_glz_reflectable<T>) {
        (void)glz::write_json(value_, out_);
      }
      ++pc;
      DISPATCH();
    }

    /** @brief フィルタ付き変数解決 */
    L_resolve_filtered: {
      auto const& instr = bc_.instructions[pc];
      auto const& var_ref = bc_.var_refs[instr.operand2];
      filtered_value_.clear();
      loop_state ls{};
      if (loop_) {
        ls.index = loop_->index;
        ls.count = loop_->count;
        ls.key = loop_->key;
        ls.in_loop = true;
      }
      if (!resolve_value(filtered_value_, var_ref.key, value_, loop_ ? &ls : nullptr)) {
        return std::unexpected(error_ctx{.ec = error_code::unknown_key});
      }
      ++pc;
      DISPATCH();
    }

    /** @brief ASCII大文字変換 */
    L_filter_upper: {
      apply_string_filter(filtered_value_, {.filter = string_filter::upper});
      ++pc;
      DISPATCH();
    }

    /** @brief ASCII小文字変換 */
    L_filter_lower: {
      apply_string_filter(filtered_value_, {.filter = string_filter::lower});
      ++pc;
      DISPATCH();
    }

    /** @brief 先頭の文字を大文字にする */
    L_filter_capitalize: {
      apply_string_filter(filtered_value_, {.filter = string_filter::capitalize});
      ++pc;
      DISPATCH();
    }

    /** @brief 単語の先頭を大文字にする */
    L_filter_title: {
      apply_string_filter(filtered_value_, {.filter = string_filter::title});
      ++pc;
      DISPATCH();
    }

    /** @brief 先頭末尾の空白除去 */
    L_filter_trim: {
      apply_string_filter(filtered_value_, {.filter = string_filter::trim});
      ++pc;
      DISPATCH();
    }

    /** @brief 先頭の空白除去 */
    L_filter_ltrim: {
      apply_string_filter(filtered_value_, {.filter = string_filter::ltrim});
      ++pc;
      DISPATCH();
    }

    /** @brief 末尾の空白除去 */
    L_filter_rtrim: {
      apply_string_filter(filtered_value_, {.filter = string_filter::rtrim});
      ++pc;
      DISPATCH();
    }

    /** @brief 左寄せ（引数: 幅） */
    L_filter_left: {
      apply_string_filter(filtered_value_, {.filter = string_filter::left, .arg1 = static_cast<int>(bc_.instructions[pc].operand)});
      ++pc;
      DISPATCH();
    }

    /** @brief 右寄せ（引数: 幅） */
    L_filter_right: {
      apply_string_filter(filtered_value_, {.filter = string_filter::right, .arg1 = static_cast<int>(bc_.instructions[pc].operand)});
      ++pc;
      DISPATCH();
    }

    /** @brief 中央寄せ（引数: 幅） */
    L_filter_center: {
      apply_string_filter(filtered_value_, {.filter = string_filter::center, .arg1 = static_cast<int>(bc_.instructions[pc].operand)});
      ++pc;
      DISPATCH();
    }

    /** @brief 文字列切り詰め（引数: 最大文字数） */
    L_filter_truncate: {
      apply_string_filter(filtered_value_, {.filter = string_filter::truncate, .arg1 = static_cast<int>(bc_.instructions[pc].operand)});
      ++pc;
      DISPATCH();
    }

    /** @brief 部分文字列（引数1: 開始位置, 引数2: 文字数） */
    L_filter_substr: {
      apply_string_filter(filtered_value_, {.filter = string_filter::substr, .arg1 = static_cast<int>(bc_.instructions[pc].operand), .arg2 = static_cast<int>(bc_.instructions[pc].operand2)});
      ++pc;
      DISPATCH();
    }

    /** @brief フィルタ後の文字列出力（エスケープあり） */
    L_emit_filtered: {
      html_escape_into(out_, std::string_view{filtered_value_});
      ++pc;
      DISPATCH();
    }

    /** @brief フィルタ後の文字列出力（生出力） */
    L_emit_filtered_raw: {
      out_.append(filtered_value_);
      ++pc;
      DISPATCH();
    }

    /** @brief 整数絶対値変換 */
    L_filter_int_abs: {
      if (auto err = apply_int_filter(filtered_value_, {.filter = int_filter::abs}); !err) {
        return std::unexpected(err.error());
      }
      ++pc;
      DISPATCH();
    }

    /** @brief 整数16進数変換 */
    L_filter_int_hex: {
      if (auto err = apply_int_filter(filtered_value_, {.filter = int_filter::hex}); !err) {
        return std::unexpected(err.error());
      }
      ++pc;
      DISPATCH();
    }

    /** @brief 整数8進数変換 */
    L_filter_int_oct: {
      if (auto err = apply_int_filter(filtered_value_, {.filter = int_filter::oct}); !err) {
        return std::unexpected(err.error());
      }
      ++pc;
      DISPATCH();
    }

    /** @brief 整数2進数変換 */
    L_filter_int_bin: {
      if (auto err = apply_int_filter(filtered_value_, {.filter = int_filter::bin}); !err) {
        return std::unexpected(err.error());
      }
      ++pc;
      DISPATCH();
    }

    /** @brief 整数符号逆転 */
    L_filter_int_neg: {
      if (auto err = apply_int_filter(filtered_value_, {.filter = int_filter::neg}); !err) {
        return std::unexpected(err.error());
      }
      ++pc;
      DISPATCH();
    }

    /** @brief 整数余り（引数: 除数） */
    L_filter_int_mod: {
      if (auto err = apply_int_filter(filtered_value_, {.filter = int_filter::mod, .arg = static_cast<int>(bc_.instructions[pc].operand)}); !err) {
        return std::unexpected(err.error());
      }
      ++pc;
      DISPATCH();
    }

    /** @brief 整数3桁カンマ区切り */
    L_filter_int_numify: {
      if (auto err = apply_int_filter(filtered_value_, {.filter = int_filter::numify}); !err) {
        return std::unexpected(err.error());
      }
      ++pc;
      DISPATCH();
    }

    /** @brief 整数0埋め（引数: 最小桁数） */
    L_filter_int_zerofill: {
      if (auto err = apply_int_filter(filtered_value_, {.filter = int_filter::zerofill, .arg = static_cast<int>(bc_.instructions[pc].operand)}); !err) {
        return std::unexpected(err.error());
      }
      ++pc;
      DISPATCH();
    }

    /** @brief 実数小数点以下桁数（引数: 桁数） */
    L_filter_float_precision: {
      apply_float_filter(filtered_value_, {.filter = float_filter::precision, .arg = static_cast<int>(bc_.instructions[pc].operand)});
      ++pc;
      DISPATCH();
    }

    /** @brief 負数判定: "true"/"false" を出力 */
    L_filter_int_is_neg: {
      if (auto err = apply_int_filter(filtered_value_, {.filter = int_filter::is_neg}); !err) {
        return std::unexpected(err.error());
      }
      ++pc;
      DISPATCH();
    }

    /** @brief 等価判定: 値と引数が等しければ "true"、そうでなければ "false" を出力 */
    L_filter_int_eq: {
      if (auto err = apply_int_filter(filtered_value_, {.filter = int_filter::eq, .arg = static_cast<int>(bc_.instructions[pc].operand)}); !err) {
        return std::unexpected(err.error());
      }
      ++pc;
      DISPATCH();
    }

    /** @brief フィルタ適用済み値での if 分岐 */
    L_emit_if_filtered: {
      auto const& instr = bc_.instructions[pc];
      bool cond = !filtered_value_.empty() && filtered_value_ != "false" && filtered_value_ != "0";
      if (!cond) {
        pc = instr.operand;
      } else {
        ++pc;
      }
      DISPATCH();
    }

    /** @brief ループ脱出: break_flag をセットして子 executor を終了 */
    L_emit_break: {
      if (loop_) {
        loop_->break_flag = true;
      }
      return {};
    }

    /** @brief 次のイテレーションへスキップ: continue_flag をセットして子 executor を終了 */
    L_emit_continue: {
      if (loop_) {
        loop_->continue_flag = true;
      }
      return {};
    }

    /** @brief プログラム終端 */
    L_halt: {
      return {};
    }

#undef DISPATCH

#else
    /**
     * フォールバック: switch ベースの分岐
     * GCC 以外のコンパイラ（Clang, MSVC 等）向けの汎用実装。
     * computed goto が利用できない環境でも同じ動作を提供する。
     */
    while (pc < end) {
      auto const& instr = bc_.instructions[pc];

      switch (instr.op) {
        /** @brief リテラル文字列を出力する */
        case bc_opcode::emit_literal: {
          out_.append(bc_.literals[instr.operand]);
          ++pc;
          break;
        }

        /** @brief 変数を出力する（raw / escaped） */
        case bc_opcode::emit_var:
        case bc_opcode::emit_var_raw: {
          auto const& ref = bc_.var_refs[instr.operand];
          auto r = for_each_field(value_, ref.key, ref.field_index, [&](auto const& field) {
            emit_var_value(field, instr.op == bc_opcode::emit_var_raw);
          });
          if (!r) return r;
          ++pc;
          break;
        }

        /** @brief セクションブロックの開始 */
        case bc_opcode::emit_section: {
          auto const& ref = bc_.var_refs[instr.operand2];
          auto body_end = instr.operand;

          auto r = for_each_field(value_, ref.key, ref.field_index,
              [&](auto const& field) -> std::expected<void, error_ctx> {
            using FT = std::remove_cvref_t<decltype(field)>;
            if constexpr (ct_is_vector_like<FT>) {
              using elem_t = typename FT::value_type;
              bc_loop_state ls;
              ls.count = static_cast<std::uint32_t>(field.size());

              for (ls.index = 0; ls.index < static_cast<std::uint32_t>(field.size()); ++ls.index) {
                ls.continue_flag = false;
                bc_executor<elem_t, RootT> child_exec(bc_, field[ls.index], root_value_, &ls, out_);
                auto r2 = child_exec.execute_impl(pc + 1, body_end - 1);
                if (!r2) return r2;
                if (ls.continue_flag) {
                  ls.continue_flag = false;
                  continue;
                }
                if (ls.break_flag) break;
              }
             } else if constexpr (std::same_as<FT, bool>) {
                if (field) {
                  auto r2 = execute_impl(pc + 1, body_end - 1);
                  if (!r2) return r2;
                }
              } else if constexpr (is_std_optional_v<FT>) {
                if (field.has_value()) {
                  using inner_t = typename FT::value_type;
                  bc_executor<inner_t, RootT> child_exec(bc_, *field, root_value_, nullptr, out_);
                  auto r2 = child_exec.execute_impl(pc + 1, body_end - 1);
                  if (!r2) return r2;
                }
              } else if constexpr (ct_is_map_like<FT>) {
                /** map の場合: キーを @key として各要素をループ */
                bc_loop_state ls;
                ls.count = static_cast<std::uint32_t>(field.size());
                for (auto const& [k, v] : field) {
                  ls.key = std::string_view{k};
                  using val_t = std::remove_cvref_t<decltype(v)>;
                  bc_executor<val_t, RootT> child_exec(bc_, v, root_value_, &ls, out_);
                  auto r2 = child_exec.execute_impl(pc + 1, body_end - 1);
                  if (!r2) return r2;
                  if (ls.break_flag) break;
                  ++ls.index;
                }
              } else if constexpr (ct_is_set_like<FT>) {
                /** set の場合: 各要素を {{this}} としてイテレータベースでループ */
                using elem_t = typename FT::value_type;
                bc_loop_state ls;
                ls.count = static_cast<std::uint32_t>(field.size());
                for (auto const& elem : field) {
                  ls.continue_flag = false;
                  bc_executor<elem_t, RootT> child_exec(bc_, elem, root_value_, &ls, out_);
                  auto r2 = child_exec.execute_impl(pc + 1, body_end - 1);
                  if (!r2) return r2;
                  if (ls.continue_flag) {
                    ls.continue_flag = false;
                    continue;
                  }
                  if (ls.break_flag) break;
                  ++ls.index;
                }
              } else if constexpr (ct_glz_reflectable<FT>) {
               constexpr auto sz = glz::reflect<FT>::size;
               auto tied = glz::to_tie(field);
               std::expected<void, error_ctx> res{};
               [&]<std::size_t... I>(std::index_sequence<I...>) {
                 (([&] {
                   if (!res) return;
                   bc_loop_state ls;
                   ls.count = sz;
                   ls.index = I;
                   ls.key = glz::reflect<FT>::keys[I];
                   using elem_t = std::remove_cvref_t<decltype(glz::get<I>(tied))>;
                   bc_executor<elem_t, RootT> child_exec(bc_, glz::get<I>(tied), root_value_, &ls, out_);
                   res = child_exec.execute_impl(pc + 1, body_end - 1);
                 }()), ...);
               }(std::make_index_sequence<sz>{});
               return res;
             }
             return {};
           });
           if (!r) return r;
           pc = body_end;
           break;
         }

        /** @brief 実行終端 */
        case bc_opcode::emit_end: {
          return {};
        }

        /** @brief 逆セクションの開始 */
        case bc_opcode::emit_inverted: {
          auto const& ref = bc_.var_refs[instr.operand2];
          bool empty = true;
          (void)for_each_field(value_, ref.key, ref.field_index, [&](auto const& field) {
            using FT = std::remove_cvref_t<decltype(field)>;
            if constexpr (ct_is_vector_like<FT>) {
              empty = field.empty();
            } else if constexpr (std::same_as<FT, bool>) {
              empty = !field;
            } else if constexpr (is_std_optional_v<FT>) {
              empty = !field.has_value();
            } else if constexpr (ct_is_map_like<FT>) {
              empty = field.empty();
            } else if constexpr (ct_is_set_like<FT>) {
              empty = field.empty();
            }
          });
          if (empty) {
            auto body_end = instr.operand;
            auto r = execute_impl(pc + 1, body_end - 1);
            if (!r) return r;
            pc = body_end;
          } else {
            pc = instr.operand;
          }
          break;
        }

        /** @brief @var 条件セクション */
        case bc_opcode::emit_at_section: {
          bool cond = false;
          if (loop_) {
            auto kind = instr.operand2;
            if (kind == 0) {
              cond = loop_->index > 0;
            } else if (kind == 1) {
              cond = loop_->index == 0;
            } else if (kind == 2) {
              cond = loop_->index + 1 == loop_->count;
            }
          }
          if (cond) {
            auto body_end = instr.operand;
            auto r = execute_impl(pc + 1, body_end - 1);
            if (!r) return r;
            pc = body_end;
          } else {
            pc = instr.operand;
          }
          break;
        }

        /** @brief @var 逆セクション */
        case bc_opcode::emit_at_inverted: {
          bool cond = false;
          if (loop_) {
            auto kind = instr.operand2;
          if (kind == 0) {
            cond = loop_->index == 0;
            } else if (kind == 1) {
              cond = loop_->index == 0;
            } else if (kind == 2) {
              cond = loop_->index + 1 == loop_->count;
            }
          }
          if (cond) {
            pc = instr.operand;
          } else {
            auto body_end = instr.operand;
            auto r = execute_impl(pc + 1, body_end - 1);
            if (!r) return r;
            pc = body_end;
          }
          break;
        }

        /** @brief ループ @index を出力する */
        case bc_opcode::emit_at_index: {
          if (loop_) {
            std::array<char, 16> buf;
            auto [ptr, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), loop_->index);
            if (ec == std::errc{}) {
              out_.append(buf.data(), ptr);
            }
          }
          ++pc;
          break;
        }

        /** @brief ループ先頭なら "true" を出力する */
        case bc_opcode::emit_at_first: {
          if (loop_ && loop_->index == 0) {
            out_.append("true");
          } else {
            out_.append("false");
          }
          ++pc;
          break;
        }

        /** @brief ループ末尾なら "true" を出力する */
        case bc_opcode::emit_at_last: {
          if (loop_ && loop_->index + 1 == loop_->count) {
            out_.append("true");
          } else {
            out_.append("false");
          }
          ++pc;
          break;
        }

        /** @brief if 条件分岐 */
        case bc_opcode::emit_if: {
          auto const& ref = bc_.var_refs[instr.operand2];
          bool cond = false;

          if (ref.key.starts_with("@")) {
            if (loop_) {
              if (ref.key == "@last") {
                cond = (loop_->index + 1 == loop_->count);
              } else if (ref.key == "@first") {
                cond = (loop_->index == 0);
              } else if (ref.key == "@index") {
                cond = (loop_->index != 0);
              }
            }
          } else {
            (void)for_each_field(value_, ref.key, ref.field_index, [&](auto const& field) {
              using FT = std::remove_cvref_t<decltype(field)>;
              if constexpr (std::same_as<FT, bool>) {
                cond = field;
              } else if constexpr (ct_is_vector_like<FT>) {
                cond = !field.empty();
              } else if constexpr (std::is_arithmetic_v<FT>) {
                cond = (field != 0);
              } else if constexpr (is_std_optional_v<FT>) {
                cond = field.has_value();
              } else if constexpr (ct_is_map_like<FT>) {
                cond = !field.empty();
              } else if constexpr (ct_is_set_like<FT>) {
                cond = !field.empty();
              }
            });
          }

          if (!cond) {
            pc = instr.operand;
          } else {
            ++pc;
          }
          break;
        }

        /** @brief else ブランチ */
        case bc_opcode::emit_else: {
          pc = instr.operand;
          break;
        }

        /** @brief endif ブロック終端 */
        case bc_opcode::emit_endif: {
          ++pc;
          break;
        }

        /** @brief 融合命令: リテラル + 変数 */
        case bc_opcode::emit_litvar:
        case bc_opcode::emit_litvar_raw: {
          out_.append(bc_.literals[instr.operand]);
          auto const& ref = bc_.var_refs[instr.operand2];
          auto r = for_each_field(value_, ref.key, ref.field_index, [&](auto const& field) {
            emit_var_value(field, instr.op == bc_opcode::emit_litvar_raw);
          });
          if (!r) return r;
          ++pc;
          break;
        }

        /** @brief @root: ルートコンテキスト全体をシリアライズして出力する */
        case bc_opcode::emit_at_root: {
          if constexpr (serializable_v<RootT>) {
            serialize_value(out_, root_value_);
          }
          ++pc;
          break;
        }

        /** @brief @root.field: ルートコンテキストのフィールドを解決して出力する */
        case bc_opcode::emit_at_root_field:
        case bc_opcode::emit_at_root_field_raw: {
          auto const& ref = bc_.var_refs[instr.operand];
          auto r = for_each_field(root_value_, ref.key, ref.field_index, [&](auto const& field) {
            emit_var_value(field, instr.op == bc_opcode::emit_at_root_field_raw);
          });
          if (!r) return r;
          ++pc;
          break;
        }

        /** @brief @key: ループ内の現在要素キー名を出力する */
        case bc_opcode::emit_at_key: {
          if (loop_) {
            if (!loop_->key.empty()) {
              out_.append(loop_->key);
            } else {
              std::array<char, 16> buf;
              auto [ptr, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), loop_->index);
              if (ec == std::errc{}) {
                out_.append(buf.data(), ptr);
              }
            }
          }
          ++pc;
          break;
        }

        /** @brief {{this}}: 現在のコンテキスト自体を出力する */
        case bc_opcode::emit_this: {
          if constexpr (serializable_v<T>) {
            serialize_value(out_, value_);
          } else if constexpr (ct_glz_reflectable<T>) {
            (void)glz::write_json(value_, out_);
          }
          ++pc;
          break;
        }

        /** @brief フィルタ付き変数解決 */
        case bc_opcode::resolve_filtered: {
          auto const& var_ref = bc_.var_refs[instr.operand2];
          filtered_value_.clear();
          loop_state ls{};
          if (loop_) {
            ls.index = loop_->index;
            ls.count = loop_->count;
            ls.key = loop_->key;
            ls.in_loop = true;
          }
          if (!resolve_value(filtered_value_, var_ref.key, value_, loop_ ? &ls : nullptr)) {
            return std::unexpected(error_ctx{.ec = error_code::unknown_key});
          }
          ++pc;
          break;
        }

        /** @brief ASCII大文字変換 */
        case bc_opcode::filter_upper: {
          apply_string_filter(filtered_value_, {.filter = string_filter::upper});
          ++pc;
          break;
        }

        /** @brief ASCII小文字変換 */
        case bc_opcode::filter_lower: {
          apply_string_filter(filtered_value_, {.filter = string_filter::lower});
          ++pc;
          break;
        }

        /** @brief 先頭の文字を大文字にする */
        case bc_opcode::filter_capitalize: {
          apply_string_filter(filtered_value_, {.filter = string_filter::capitalize});
          ++pc;
          break;
        }

        /** @brief 単語の先頭を大文字にする */
        case bc_opcode::filter_title: {
          apply_string_filter(filtered_value_, {.filter = string_filter::title});
          ++pc;
          break;
        }

        /** @brief 先頭末尾の空白除去 */
        case bc_opcode::filter_trim: {
          apply_string_filter(filtered_value_, {.filter = string_filter::trim});
          ++pc;
          break;
        }

        /** @brief 先頭の空白除去 */
        case bc_opcode::filter_ltrim: {
          apply_string_filter(filtered_value_, {.filter = string_filter::ltrim});
          ++pc;
          break;
        }

        /** @brief 末尾の空白除去 */
        case bc_opcode::filter_rtrim: {
          apply_string_filter(filtered_value_, {.filter = string_filter::rtrim});
          ++pc;
          break;
        }

        /** @brief 左寄せ（引数: 幅） */
        case bc_opcode::filter_left: {
          apply_string_filter(filtered_value_, {.filter = string_filter::left, .arg1 = static_cast<int>(instr.operand)});
          ++pc;
          break;
        }

        /** @brief 右寄せ（引数: 幅） */
        case bc_opcode::filter_right: {
          apply_string_filter(filtered_value_, {.filter = string_filter::right, .arg1 = static_cast<int>(instr.operand)});
          ++pc;
          break;
        }

        /** @brief 中央寄せ（引数: 幅） */
        case bc_opcode::filter_center: {
          apply_string_filter(filtered_value_, {.filter = string_filter::center, .arg1 = static_cast<int>(instr.operand)});
          ++pc;
          break;
        }

        /** @brief 文字列切り詰め（引数: 最大文字数） */
        case bc_opcode::filter_truncate: {
          apply_string_filter(filtered_value_, {.filter = string_filter::truncate, .arg1 = static_cast<int>(instr.operand)});
          ++pc;
          break;
        }

        /** @brief 部分文字列（引数1: 開始位置, 引数2: 文字数） */
        case bc_opcode::filter_substr: {
          apply_string_filter(filtered_value_, {.filter = string_filter::substr, .arg1 = static_cast<int>(instr.operand), .arg2 = static_cast<int>(instr.operand2)});
          ++pc;
          break;
        }

        /** @brief フィルタ後の文字列出力（エスケープあり） */
        case bc_opcode::emit_filtered: {
          html_escape_into(out_, std::string_view{filtered_value_});
          ++pc;
          break;
        }

        /** @brief フィルタ後の文字列出力（生出力） */
        case bc_opcode::emit_filtered_raw: {
          out_.append(filtered_value_);
          ++pc;
          break;
        }

        /** @brief 整数絶対値変換 */
        case bc_opcode::filter_int_abs: {
          if (auto err = apply_int_filter(filtered_value_, {.filter = int_filter::abs}); !err) {
            return std::unexpected(err.error());
          }
          ++pc;
          break;
        }

        /** @brief 整数16進数変換 */
        case bc_opcode::filter_int_hex: {
          if (auto err = apply_int_filter(filtered_value_, {.filter = int_filter::hex}); !err) {
            return std::unexpected(err.error());
          }
          ++pc;
          break;
        }

        /** @brief 整数8進数変換 */
        case bc_opcode::filter_int_oct: {
          if (auto err = apply_int_filter(filtered_value_, {.filter = int_filter::oct}); !err) {
            return std::unexpected(err.error());
          }
          ++pc;
          break;
        }

        /** @brief 整数2進数変換 */
        case bc_opcode::filter_int_bin: {
          if (auto err = apply_int_filter(filtered_value_, {.filter = int_filter::bin}); !err) {
            return std::unexpected(err.error());
          }
          ++pc;
          break;
        }

        /** @brief 整数符号逆転 */
        case bc_opcode::filter_int_neg: {
          if (auto err = apply_int_filter(filtered_value_, {.filter = int_filter::neg}); !err) {
            return std::unexpected(err.error());
          }
          ++pc;
          break;
        }

        /** @brief 整数余り（引数: 除数） */
        case bc_opcode::filter_int_mod: {
          if (auto err = apply_int_filter(filtered_value_, {.filter = int_filter::mod, .arg = static_cast<int>(instr.operand)}); !err) {
            return std::unexpected(err.error());
          }
          ++pc;
          break;
        }

        /** @brief 整数3桁カンマ区切り */
        case bc_opcode::filter_int_numify: {
          if (auto err = apply_int_filter(filtered_value_, {.filter = int_filter::numify}); !err) {
            return std::unexpected(err.error());
          }
          ++pc;
          break;
        }

        /** @brief 実数小数点以下桁数（引数: 桁数） */
        case bc_opcode::filter_float_precision: {
          apply_float_filter(filtered_value_, {.filter = float_filter::precision, .arg = static_cast<int>(instr.operand)});
          ++pc;
          break;
        }

        /** @brief 負数判定: "true"/"false" を出力 */
        case bc_opcode::filter_int_is_neg: {
          if (auto err = apply_int_filter(filtered_value_, {.filter = int_filter::is_neg}); !err) {
            return std::unexpected(err.error());
          }
          ++pc;
          break;
        }

        /** @brief 等価判定: 値と引数が等しければ "true" / "false" */
        case bc_opcode::filter_int_eq: {
          if (auto err = apply_int_filter(filtered_value_, {.filter = int_filter::eq, .arg = static_cast<int>(instr.operand)}); !err) {
            return std::unexpected(err.error());
          }
          ++pc;
          break;
        }

        /** @brief 整数0埋め（引数: 最小桁数） */
        case bc_opcode::filter_int_zerofill: {
          if (auto err = apply_int_filter(filtered_value_, {.filter = int_filter::zerofill, .arg = static_cast<int>(instr.operand)}); !err) {
            return std::unexpected(err.error());
          }
          ++pc;
          break;
        }

        /** @brief フィルタ適用済み値での if 分岐 */
        case bc_opcode::emit_if_filtered: {
          bool cond = !filtered_value_.empty() && filtered_value_ != "false" && filtered_value_ != "0";
          if (!cond) {
            pc = instr.operand;
          } else {
            ++pc;
          }
          break;
        }

        /** @brief ループ脱出: break_flag をセットして子 executor を終了 */
        case bc_opcode::emit_break: {
          if (loop_) {
            loop_->break_flag = true;
          }
          return {};
        }

        /** @brief 次のイテレーションへスキップ: continue_flag をセットして子 executor を終了 */
        case bc_opcode::emit_continue: {
          if (loop_) {
            loop_->continue_flag = true;
          }
          return {};
        }

        /** @brief プログラム終端 */
        case bc_opcode::halt: {
          return {};
        }
      }
    }

    return {};
#endif
  }
};

/**
 * @brief バイトコードを実行してレンダリング結果を取得する
 * @tparam T コンテキスト値の型
 * @param bc バイトコード
 * @param value コンテキスト値
 * @return std::expected<std::string, error_ctx> レンダリング結果
 */
template <class T>
std::expected<std::string, error_ctx> bc_execute(bytecode const& bc, T const& value) {
  std::string out;
  out.reserve(256);
  bc_executor<T> exec(bc, value, value, nullptr, out);
  auto r = exec.execute();
  if (!r) {
    return std::unexpected(r.error());
  }
  return out;
}

} // namespace injamm::detail
