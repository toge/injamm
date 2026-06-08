#pragma once

#include "bytecode.hpp"
#include "escape.hpp"
#include "serialize_value.hpp"
#include "../injamm.hpp"
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
  bc_loop_state const* parent = nullptr;
  /**< 親ループ状態へのポインタ。ネスト時のみ使用 */
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
          /** 単一インデックスを試行する内部ラムダ。見つかれば visitor を適用。 */
          auto try_index = [&]<std::size_t Idx>() -> bool {
            if (field_index == Idx) {
              if constexpr (std::same_as<visitor_t, void>) {
                visitor(glz::get<Idx>(tied));
              } else {
                auto r = visitor(glz::get<Idx>(tied));
                if (!r) return true;
              }
              return true;
            }
            return false;
          };
          /** fold 式で全インデックスを試行（最初の一致で短絡） */
          bool found = (try_index.template operator()<I>() || ...);
          (void)found;
          return std::expected<void, error_ctx>{};
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
      &&L_halt,              // 17
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

      auto r = for_each_field(value_, ref.key, ref.field_index,
          [&](auto const& field) -> std::expected<void, error_ctx> {
        using FT = std::remove_cvref_t<decltype(field)>;
        if constexpr (ct_is_vector_like<FT>) {
          /** 配列の場合: 各要素をループ */
          using elem_t = typename FT::value_type;
          bc_loop_state ls;
          ls.count = static_cast<std::uint32_t>(field.size());

          for (ls.index = 0; ls.index < static_cast<std::uint32_t>(field.size()); ++ls.index) {
            bc_executor<elem_t, RootT> child_exec(bc_, field[ls.index], root_value_, &ls, out_);
            auto r2 = child_exec.execute_impl(pc + 1, body_end - 1);
            if (!r2) return r2;
          }
        } else if constexpr (std::same_as<FT, bool>) {
          /** bool の場合: 真ならボディを一度描画 */
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

    /**
     * @brief bool 専用セクションブロック（最適化用）
     * @details パーサーが配列ではないことが確定している場合に emit_section の
     *          代わりに発行される。ベクター分岐を省略できる。
     */
    L_emit_section_bool: {
      auto const& instr = bc_.instructions[pc];
      auto const& ref = bc_.var_refs[instr.operand2];
      auto body_end = instr.operand;

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
      ++pc;
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
          /** @index: 常に偽（@index 逆セクションは空ループ時のみ？） */
          cond = false;
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
                bc_executor<elem_t, RootT> child_exec(bc_, field[ls.index], root_value_, &ls, out_);
                auto r2 = child_exec.execute_impl(pc + 1, body_end - 1);
                if (!r2) return r2;
              }
            } else if constexpr (std::same_as<FT, bool>) {
              if (field) {
                auto r2 = execute_impl(pc + 1, body_end - 1);
                if (!r2) return r2;
              }
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

        /** @brief @var 逆セクション */
        case bc_opcode::emit_at_inverted: {
          bool cond = false;
          if (loop_) {
            auto kind = instr.operand2;
            if (kind == 0) {
              cond = false;
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
