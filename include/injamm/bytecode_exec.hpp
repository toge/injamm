#pragma once

/**
 * @brief GCC + C++23 以上の環境では computed goto ディスパッチを自動有効化する。
 * @details CMake 経由でビルドした場合は INJAMM_THREADED_DISPATCH が定義済みのため
 *          このブロックは実質 no-op となる。ヘッダオンリーで直接インクルードする
 *          ユーザー（vcpkg 取得後に直接 include する場合など）でも自動的に
 *          computed goto の恩恵を受けられるようにする。
 *          無効化したい場合は INJAMM_NO_THREADED_DISPATCH を定義する。
 */
#if defined(__GNUC__) && !defined(__clang__) && !defined(INJAMM_NO_THREADED_DISPATCH) && !defined(INJAMM_THREADED_DISPATCH)
#define INJAMM_THREADED_DISPATCH 1
#endif

#include "../injamm.hpp"
#include "bytecode.hpp"
#include "escape.hpp"
#include "filters.hpp"
#include "glz_dispatch.hpp"
#include "serialize_value.hpp"
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
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
  bytecode const&      bc_;
  T const&             value_;
  RootT const&         root_value_;
  bc_loop_state const* loop_ = nullptr;
  std::string&         out_;
  std::string          emit_this_scratch_;

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
  auto resolve_nested_path(V const& v, std::string_view path, F&& visitor) const -> std::expected<void, error_ctx> {
    /** ドットの位置で分割: 末端に達したか判定 */
    auto dot_pos = path.find('.');
    if (dot_pos == std::string_view::npos) {
      /** 末端: パス全体をキーとして一致するフィールドを探し visitor を適用 */
      if constexpr (ct_glz_reflectable<V>) {
        constexpr auto sz   = static_cast<std::size_t>(glz::reflect<V>::size);
        auto           tied = glz::to_tie(v);
        using visitor_r     = decltype(visitor(glz::get<0>(tied)));
        if constexpr (std::same_as<visitor_r, void>) {
          /** visitor が void を返す場合: fold 式で全フィールドを走査 */
          [&]<std::size_t... I>(std::index_sequence<I...>) {
            (([&] {
               if (std::string_view{glz::reflect<V>::keys[I]} == path) {
                 visitor(glz::get<I>(tied));
               }
             }()),
             ...);
          }(std::make_index_sequence<sz>{});
        } else {
          /** visitor が std::expected を返す場合: エラーを伝搬する */
          std::expected<void, error_ctx> result{};
          [&]<std::size_t... I>(std::index_sequence<I...>) {
            (([&] {
               if (!result)
                 return;
               if (std::string_view{glz::reflect<V>::keys[I]} == path) {
                 result = visitor(glz::get<I>(tied));
               }
             }()),
             ...);
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
      constexpr auto sz   = static_cast<std::size_t>(glz::reflect<V>::size);
      auto           tied = glz::to_tie(v);
      using visitor_r     = decltype(visitor(glz::get<0>(tied)));
      if constexpr (std::same_as<visitor_r, void>) {
        /** visitor が void の場合: フィールドを発見次第再帰 */
        [&]<std::size_t... I>(std::index_sequence<I...>) {
          (([&] {
             if (std::string_view{glz::reflect<V>::keys[I]} == first_key) {
               auto const& field = glz::get<I>(tied);
               using FT          = std::remove_cvref_t<decltype(field)>;
                if constexpr (ct_glz_reflectable<FT>) {
                  (void)resolve_nested_path(field, rest_path, std::forward<F>(visitor));
                } else if constexpr (ct_is_vector_like<FT>) {
                  auto idx_dot = rest_path.find('.');
                  auto idx_str = rest_path.substr(0, idx_dot);
                  if (!idx_str.empty() && idx_str.find_first_not_of("0123456789") == std::string_view::npos) {
                    std::size_t idx = 0;
                    auto [ptr, ec] = std::from_chars(idx_str.data(), idx_str.data() + idx_str.size(), idx);
                    if (ec == std::errc{} && idx < field.size()) {
                      auto const& elem = field[idx];
                      using ET = std::remove_cvref_t<decltype(elem)>;
                      if (idx_dot == std::string_view::npos) {
                        visitor(elem);
                      } else if constexpr (ct_glz_reflectable<ET>) {
                        (void)resolve_nested_path(elem, rest_path.substr(idx_dot + 1), std::forward<F>(visitor));
                      }
                    }
                  }
                }
              }
            }()),
            ...);
        }(std::make_index_sequence<sz>{});
      } else {
        /** visitor が expected を返す場合: エラーを伝搬しながら再帰 */
        std::expected<void, error_ctx> result{};
        [&]<std::size_t... I>(std::index_sequence<I...>) {
          (([&] {
             if (!result)
               return;
             if (std::string_view{glz::reflect<V>::keys[I]} == first_key) {
               auto const& field = glz::get<I>(tied);
               using FT          = std::remove_cvref_t<decltype(field)>;
               if constexpr (ct_glz_reflectable<FT>) {
                 result = resolve_nested_path(field, rest_path, std::forward<F>(visitor));
               } else if constexpr (ct_is_vector_like<FT>) {
                 auto idx_dot = rest_path.find('.');
                 auto idx_str = rest_path.substr(0, idx_dot);
                 if (!idx_str.empty() && idx_str.find_first_not_of("0123456789") == std::string_view::npos) {
                   std::size_t idx = 0;
                   auto [ptr, ec] = std::from_chars(idx_str.data(), idx_str.data() + idx_str.size(), idx);
                   if (ec == std::errc{} && idx < field.size()) {
                     auto const& elem = field[idx];
                     using ET = std::remove_cvref_t<decltype(elem)>;
                     if (idx_dot == std::string_view::npos) {
                       result = visitor(elem);
                     } else if constexpr (ct_glz_reflectable<ET>) {
                       result = resolve_nested_path(elem, rest_path.substr(idx_dot + 1), std::forward<F>(visitor));
                     }
                   }
                 }
               }
             }
           }()),
           ...);
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
  auto for_each_field(V const& v, std::string_view key, std::uint32_t field_index, bool has_dot, F&& visitor) const -> std::expected<void, error_ctx> {
    /** ネストパスが含まれている場合は再帰解決に委譲 */
    if (has_dot) {
      return resolve_nested_path(v, key, std::forward<F>(visitor));
    }

    if constexpr (ct_glz_reflectable<V>) {
      constexpr auto sz   = static_cast<std::size_t>(glz::reflect<V>::size);
      auto           tied = glz::to_tie(v);
      using visitor_t     = decltype(visitor(glz::get<0>(tied)));

      /**
       * O(1) アクセス: field_index が有効な場合
       * if constexpr チェーンで実行時の field_index 値をコンパイル時定数に変換し、
       * 該当フィールドに直接アクセスする。コンパイラが二分探索的ジャンプテーブルを生成。
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
        bool found = false;
        [&]<std::size_t... I>(std::index_sequence<I...>) {
          (([&] {
             if (std::string_view{glz::reflect<V>::keys[I]} == key) {
               visitor(glz::get<I>(tied));
               found = true;
             }
           }()),
           ...);
        }(std::make_index_sequence<sz>{});
        if (!found && !key.empty() && !key.starts_with('@'))
          return std::unexpected(error_ctx{.ec = error_code::unknown_key});
        return {};
      } else {
        std::expected<void, error_ctx> result{};
        [&]<std::size_t... I>(std::index_sequence<I...>) {
          (([&] {
             if (!result)
               return;
             if (std::string_view{glz::reflect<V>::keys[I]} == key) {
               result = visitor(glz::get<I>(tied));
             }
           }()),
           ...);
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
        out_.append("true", 4);
      } else {
        out_.append("false", 5);
      }
    } else if constexpr (std::same_as<FT, std::string> || std::same_as<FT, std::string_view>) {
      if (raw) {
        out_.append(field.data(), field.size());
      } else {
        html_escape_into(out_, field);
      }
    } else if constexpr (std::is_arithmetic_v<FT> && !std::same_as<FT, bool>) {
      /** 整数/浮動小数の高速 append: traits::copy 経由の memmove を避けて push_back ループで書き出す */
      std::array<char, 32> buf;
      auto [ptr, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), field);
      if (ec == std::errc{}) {
        auto const n = static_cast<std::size_t>(ptr - buf.data());
        out_.append(buf.data(), n);
      }
    } else if constexpr (is_std_optional_v<FT>) {
      if (field.has_value()) {
        emit_var_value(*field, raw);
      }
    }
  }

  // -- handler functions (shared by both dispatch paths) --

  static std::expected<void, error_ctx> handle_emit_literal(bc_executor& ex, std::size_t& pc, std::string&) {
    ex.out_.append(ex.bc_.literals[ex.bc_.instructions[pc].operand]);
    ++pc;
    return {};
  }

  static std::expected<void, error_ctx> handle_emit_var(bc_executor& ex, std::size_t& pc, std::string&) {
    bool raw = ex.bc_.instructions[pc].op == bc_opcode::emit_var_raw;
    auto const& ref = ex.bc_.var_refs[ex.bc_.instructions[pc].operand];
    auto r = ex.for_each_field(ex.value_, ref.key, ref.field_index, ref.has_dot, [&](auto const& field) { ex.emit_var_value(field, raw); });
    if (!r) return std::unexpected(r.error());
    ++pc;
    return {};
  }

  static std::expected<void, error_ctx> handle_emit_litvar(bc_executor& ex, std::size_t& pc, std::string&) {
    bool raw = ex.bc_.instructions[pc].op == bc_opcode::emit_litvar_raw;
    ex.out_.append(ex.bc_.literals[ex.bc_.instructions[pc].operand]);
    auto const& ref = ex.bc_.var_refs[ex.bc_.instructions[pc].operand2];
    auto r = ex.for_each_field(ex.value_, ref.key, ref.field_index, ref.has_dot, [&](auto const& field) { ex.emit_var_value(field, raw); });
    if (!r) return std::unexpected(r.error());
    ++pc;
    return {};
  }

  static std::expected<void, error_ctx> handle_emit_at_root(bc_executor& ex, std::size_t& pc, std::string&) {
    if constexpr (serializable_v<RootT>) {
      serialize_value(ex.out_, ex.root_value_);
    }
    ++pc;
    return {};
  }

  static std::expected<void, error_ctx> handle_emit_at_root_field(bc_executor& ex, std::size_t& pc, std::string&) {
    bool raw = ex.bc_.instructions[pc].op == bc_opcode::emit_at_root_field_raw;
    auto const& ref = ex.bc_.var_refs[ex.bc_.instructions[pc].operand];
    auto r = ex.for_each_field(ex.root_value_, ref.key, ref.field_index, ref.has_dot, [&](auto const& field) { ex.emit_var_value(field, raw); });
    if (!r) return std::unexpected(r.error());
    ++pc;
    return {};
  }

  static std::expected<void, error_ctx> handle_emit_this(bc_executor& ex, std::size_t& pc, std::string&) {
    ex.emit_this_scratch_.clear();
    if constexpr (serializable_v<T>) {
      serialize_value(ex.emit_this_scratch_, ex.value_);
    } else if constexpr (ct_glz_reflectable<T> && glz::write_supported<T, glz::JSON>) {
      if (auto ec = glz::write_json(ex.value_, ex.emit_this_scratch_)) {
        return std::unexpected(error_ctx{.position = pc, .ec = error_code::syntax_error});
      }
    }
    html_escape_into(ex.out_, ex.emit_this_scratch_);
    ++pc;
    return {};
  }

  static std::expected<void, error_ctx> handle_emit_var_size(bc_executor& ex, std::size_t& pc, std::string&) {
    auto const& ref = ex.bc_.var_refs[ex.bc_.instructions[pc].operand];
    auto r = ex.for_each_field(ex.value_, ref.key, ref.field_index, ref.has_dot, [&](auto const& field) {
      using FT = std::remove_cvref_t<decltype(field)>;
      std::size_t sz = 0;
      if constexpr (ct_is_vector_like<FT>) {
        sz = field.size();
      } else if constexpr (ct_is_map_like<FT>) {
        sz = field.size();
      } else if constexpr (ct_is_set_like<FT>) {
        sz = field.size();
      } else if constexpr (std::is_arithmetic_v<FT>) {
        sz = 0;
      }
      std::array<char, 16> buf;
      auto [ptr, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), sz);
      if (ec == std::errc{}) {
        ex.out_.append(buf.data(), ptr);
      }
    });
    if (!r) return std::unexpected(r.error());
    ++pc;
    return {};
  }

  static std::expected<void, error_ctx> handle_emit_break(bc_executor& ex, std::size_t& pc, std::string&) {
    if (ex.loop_) ex.loop_->break_flag = true;
    pc = SIZE_MAX;
    return {};
  }

  static std::expected<void, error_ctx> handle_emit_continue(bc_executor& ex, std::size_t& pc, std::string&) {
    if (ex.loop_) ex.loop_->continue_flag = true;
    pc = SIZE_MAX;
    return {};
  }

  static std::expected<void, error_ctx> handle_emit_at_index(bc_executor& ex, std::size_t& pc, std::string&) {
    if (ex.loop_) {
      std::array<char, 16> buf;
      auto [ptr, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), ex.loop_->index);
      if (ec == std::errc{}) ex.out_.append(buf.data(), ptr);
    }
    ++pc;
    return {};
  }

  static std::expected<void, error_ctx> handle_emit_at_index1(bc_executor& ex, std::size_t& pc, std::string&) {
    if (ex.loop_) {
      std::array<char, 16> buf;
      auto [ptr, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), ex.loop_->index + 1);
      if (ec == std::errc{}) ex.out_.append(buf.data(), ptr);
    }
    ++pc;
    return {};
  }

  static std::expected<void, error_ctx> handle_emit_at_size(bc_executor& ex, std::size_t& pc, std::string&) {
    if (ex.loop_) {
      std::array<char, 16> buf;
      auto [ptr, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), ex.loop_->count);
      if (ec == std::errc{}) ex.out_.append(buf.data(), ptr);
    }
    ++pc;
    return {};
  }

  static std::expected<void, error_ctx> handle_emit_at_first(bc_executor& ex, std::size_t& pc, std::string&) {
    if (ex.loop_ && ex.loop_->index == 0) { ex.out_.append("true"); } else { ex.out_.append("false"); }
    ++pc;
    return {};
  }

  static std::expected<void, error_ctx> handle_emit_at_last(bc_executor& ex, std::size_t& pc, std::string&) {
    if (ex.loop_ && ex.loop_->index + 1 == ex.loop_->count) { ex.out_.append("true"); } else { ex.out_.append("false"); }
    ++pc;
    return {};
  }

  static std::expected<void, error_ctx> handle_emit_at_key(bc_executor& ex, std::size_t& pc, std::string&) {
    if (ex.loop_) {
      if (!ex.loop_->key.empty()) {
        ex.out_.append(ex.loop_->key);
      } else {
        std::array<char, 16> buf;
        auto [ptr, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), ex.loop_->index);
        if (ec == std::errc{}) ex.out_.append(buf.data(), ptr);
      }
    }
    ++pc;
    return {};
  }

  /** @brief ディスパッチテーブル用: filter_upper / lower / capitalize / title / trim / ltrim / rtrim / replace */
  static std::expected<void, error_ctx> handle_string_filter(bc_executor& ex, std::size_t& pc, std::string& filtered) {
    auto op = ex.bc_.instructions[pc].op;
    switch (op) {
    case bc_opcode::filter_upper:      apply_string_filter(filtered, {.filter = string_filter::upper}); break;
    case bc_opcode::filter_lower:      apply_string_filter(filtered, {.filter = string_filter::lower}); break;
    case bc_opcode::filter_capitalize: apply_string_filter(filtered, {.filter = string_filter::capitalize}); break;
    case bc_opcode::filter_title:      apply_string_filter(filtered, {.filter = string_filter::title}); break;
    case bc_opcode::filter_trim:       apply_string_filter(filtered, {.filter = string_filter::trim}); break;
    case bc_opcode::filter_ltrim:      apply_string_filter(filtered, {.filter = string_filter::ltrim}); break;
    case bc_opcode::filter_rtrim:      apply_string_filter(filtered, {.filter = string_filter::rtrim}); break;
    case bc_opcode::filter_replace:    apply_string_filter(filtered, {.filter = string_filter::replace}); break;
    default: return std::unexpected(error_ctx{.position = pc, .ec = error_code::syntax_error});
    }
    ++pc;
    return {};
  }

  /** @brief ディスパッチテーブル用: filter_left / right / center / truncate（1引数） */
  static std::expected<void, error_ctx> handle_string_filter_arg(bc_executor& ex, std::size_t& pc, std::string& filtered) {
    auto op  = ex.bc_.instructions[pc].op;
    auto arg = static_cast<int>(ex.bc_.instructions[pc].operand);
    switch (op) {
    case bc_opcode::filter_left:     apply_string_filter(filtered, {.filter = string_filter::left, .arg1 = arg}); break;
    case bc_opcode::filter_right:    apply_string_filter(filtered, {.filter = string_filter::right, .arg1 = arg}); break;
    case bc_opcode::filter_center:   apply_string_filter(filtered, {.filter = string_filter::center, .arg1 = arg}); break;
    case bc_opcode::filter_truncate: apply_string_filter(filtered, {.filter = string_filter::truncate, .arg1 = arg}); break;
    default: return std::unexpected(error_ctx{.position = pc, .ec = error_code::syntax_error});
    }
    ++pc;
    return {};
  }

  /** @brief ディスパッチテーブル用: filter_substr（2引数） */
  static std::expected<void, error_ctx> handle_string_filter_arg2(bc_executor& ex, std::size_t& pc, std::string& filtered) {
    apply_string_filter(filtered, {.filter = string_filter::substr, .arg1 = static_cast<int>(ex.bc_.instructions[pc].operand), .arg2 = static_cast<int>(ex.bc_.instructions[pc].operand2)});
    ++pc;
    return {};
  }

  /** @brief ディスパッチテーブル用: 全整数フィルタ */
  static std::expected<void, error_ctx> handle_int_filter(bc_executor& ex, std::size_t& pc, std::string& filtered) {
    auto op  = ex.bc_.instructions[pc].op;
    auto arg = static_cast<int>(ex.bc_.instructions[pc].operand);
    switch (op) {
    case bc_opcode::filter_int_abs:      { auto r = apply_int_filter(filtered, {.filter = int_filter::abs}); if (!r) return std::unexpected(r.error()); } break;
    case bc_opcode::filter_int_hex:      { auto r = apply_int_filter(filtered, {.filter = int_filter::hex}); if (!r) return std::unexpected(r.error()); } break;
    case bc_opcode::filter_int_oct:      { auto r = apply_int_filter(filtered, {.filter = int_filter::oct}); if (!r) return std::unexpected(r.error()); } break;
    case bc_opcode::filter_int_bin:      { auto r = apply_int_filter(filtered, {.filter = int_filter::bin}); if (!r) return std::unexpected(r.error()); } break;
    case bc_opcode::filter_int_neg:      { auto r = apply_int_filter(filtered, {.filter = int_filter::neg}); if (!r) return std::unexpected(r.error()); } break;
    case bc_opcode::filter_int_mod:      { auto r = apply_int_filter(filtered, {.filter = int_filter::mod, .arg = arg}); if (!r) return std::unexpected(r.error()); } break;
    case bc_opcode::filter_int_numify:   { auto r = apply_int_filter(filtered, {.filter = int_filter::numify}); if (!r) return std::unexpected(r.error()); } break;
    case bc_opcode::filter_int_is_neg:   { auto r = apply_int_filter(filtered, {.filter = int_filter::is_neg}); if (!r) return std::unexpected(r.error()); } break;
    case bc_opcode::filter_int_eq:       { auto r = apply_int_filter(filtered, {.filter = int_filter::eq, .arg = arg}); if (!r) return std::unexpected(r.error()); } break;
    case bc_opcode::filter_int_ne:       { auto r = apply_int_filter(filtered, {.filter = int_filter::ne, .arg = arg}); if (!r) return std::unexpected(r.error()); } break;
    case bc_opcode::filter_int_gt:       { auto r = apply_int_filter(filtered, {.filter = int_filter::gt, .arg = arg}); if (!r) return std::unexpected(r.error()); } break;
    case bc_opcode::filter_int_gte:      { auto r = apply_int_filter(filtered, {.filter = int_filter::gte, .arg = arg}); if (!r) return std::unexpected(r.error()); } break;
    case bc_opcode::filter_int_lt:       { auto r = apply_int_filter(filtered, {.filter = int_filter::lt, .arg = arg}); if (!r) return std::unexpected(r.error()); } break;
    case bc_opcode::filter_int_lte:      { auto r = apply_int_filter(filtered, {.filter = int_filter::lte, .arg = arg}); if (!r) return std::unexpected(r.error()); } break;
    case bc_opcode::filter_int_zerofill: { auto r = apply_int_filter(filtered, {.filter = int_filter::zerofill, .arg = arg}); if (!r) return std::unexpected(r.error()); } break;
    case bc_opcode::filter_int_add:      { auto r = apply_int_filter(filtered, {.filter = int_filter::add, .arg = arg}); if (!r) return std::unexpected(r.error()); } break;
    case bc_opcode::filter_int_sub:      { auto r = apply_int_filter(filtered, {.filter = int_filter::sub, .arg = arg}); if (!r) return std::unexpected(r.error()); } break;
    case bc_opcode::filter_int_mul:      { auto r = apply_int_filter(filtered, {.filter = int_filter::mul, .arg = arg}); if (!r) return std::unexpected(r.error()); } break;
    case bc_opcode::filter_int_div:      { auto r = apply_int_filter(filtered, {.filter = int_filter::div, .arg = arg}); if (!r) return std::unexpected(r.error()); } break;
    default: return std::unexpected(error_ctx{.position = pc, .ec = error_code::syntax_error});
    }
    ++pc;
    return {};
  }

  /** @brief ディスパッチテーブル用: filter_float_precision */
  static std::expected<void, error_ctx> handle_float_filter(bc_executor& ex, std::size_t& pc, std::string& filtered) {
    apply_float_filter(filtered, {.filter = float_filter::precision, .arg = static_cast<int>(ex.bc_.instructions[pc].operand)});
    ++pc;
    return {};
  }

  static std::expected<void, error_ctx> handle_resolve_filtered(bc_executor& ex, std::size_t& pc, std::string& filtered) {
    auto const& instr  = ex.bc_.instructions[pc];
    auto const& var_ref = ex.bc_.var_refs[instr.operand2];
    filtered.clear();
    auto r = ex.for_each_field(ex.value_, var_ref.key, var_ref.field_index, var_ref.has_dot, [&](auto const& field) {
      using FT = std::remove_cvref_t<decltype(field)>;
      if constexpr (serializable_v<FT>) {
        serialize_value(filtered, field);
      } else if constexpr (is_std_optional_v<FT>) {
        if (field.has_value()) serialize_value(filtered, *field);
      }
    });
    if (!r) return std::unexpected(r.error());
    for (auto const& f : var_ref.filters) apply_string_filter(filtered, f);
    for (auto const& f : var_ref.int_filters) {
      if (auto err = apply_int_filter(filtered, f); !err) return std::unexpected(err.error());
    }
    for (auto const& f : var_ref.float_filters) apply_float_filter(filtered, f);
    ++pc;
    pc += instr.operand;
    return {};
  }

  static std::expected<void, error_ctx> handle_emit_filtered(bc_executor& ex, std::size_t& pc, std::string& filtered) {
    bool raw = ex.bc_.instructions[pc].op == bc_opcode::emit_filtered_raw;
    if (raw) { ex.out_.append(filtered); } else { html_escape_into(ex.out_, std::string_view{filtered}); }
    ++pc;
    return {};
  }

  static std::expected<void, error_ctx> handle_emit_end(bc_executor&, std::size_t& pc, std::string&) {
    ++pc;
    return {};
  }

  static std::expected<void, error_ctx> handle_emit_halt(bc_executor&, std::size_t& pc, std::string&) {
    ++pc;
    return {};
  }

  static std::expected<void, error_ctx> handle_emit_section(bc_executor& ex, std::size_t& pc, std::string&) {
    auto const& instr = ex.bc_.instructions[pc];
    auto const& ref   = ex.bc_.var_refs[instr.operand2];
    auto        body_end = instr.operand;
    if (body_end <= pc + 1 || body_end > ex.bc_.instructions.size())
      return std::unexpected(error_ctx{.position = pc, .ec = error_code::syntax_error});
    auto r = ex.for_each_field(ex.value_, ref.key, ref.field_index, ref.has_dot, [&](auto const& field) -> std::expected<void, error_ctx> {
      using FT = std::remove_cvref_t<decltype(field)>;
      if constexpr (ct_is_vector_like<FT>) {
        using elem_t = typename FT::value_type;
        bc_loop_state ls;
        ls.count = static_cast<std::uint32_t>(field.size());
        for (ls.index = 0; ls.index < static_cast<std::uint32_t>(field.size()); ++ls.index) {
          ls.continue_flag = false;
          bc_executor<elem_t, RootT> child_exec(ex.bc_, field[ls.index], ex.root_value_, &ls, ex.out_);
          auto r2 = child_exec.execute_impl(pc + 1, body_end - 1);
          if (!r2) return r2;
          if (ls.continue_flag) { ls.continue_flag = false; continue; }
          if (ls.break_flag) break;
        }
      } else if constexpr (std::same_as<FT, bool>) {
        if (field) { auto r2 = ex.execute_impl(pc + 1, body_end - 1); if (!r2) return r2; }
      } else if constexpr (is_std_optional_v<FT>) {
        if (field.has_value()) {
          using inner_t = typename FT::value_type;
          bc_executor<inner_t, RootT> child_exec(ex.bc_, *field, ex.root_value_, nullptr, ex.out_);
          auto r2 = child_exec.execute_impl(pc + 1, body_end - 1);
          if (!r2) return r2;
        }
      } else if constexpr (ct_is_map_like<FT>) {
        bc_loop_state ls;
        ls.count = static_cast<std::uint32_t>(field.size());
        for (auto const& [k, v] : field) {
          ls.key = std::string_view{k};
          using val_t = std::remove_cvref_t<decltype(v)>;
          bc_executor<val_t, RootT> child_exec(ex.bc_, v, ex.root_value_, &ls, ex.out_);
          auto r2 = child_exec.execute_impl(pc + 1, body_end - 1);
          if (!r2) return r2;
          if (ls.break_flag) break;
          ++ls.index;
        }
      } else if constexpr (ct_is_set_like<FT>) {
        using elem_t = typename FT::value_type;
        bc_loop_state ls;
        ls.count = static_cast<std::uint32_t>(field.size());
        for (auto const& elem : field) {
          ls.continue_flag = false;
          bc_executor<elem_t, RootT> child_exec(ex.bc_, elem, ex.root_value_, &ls, ex.out_);
          auto r2 = child_exec.execute_impl(pc + 1, body_end - 1);
          if (!r2) return r2;
          if (ls.continue_flag) { ls.continue_flag = false; continue; }
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
            ls.count = sz; ls.index = I; ls.key = glz::reflect<FT>::keys[I];
            using elem_t = std::remove_cvref_t<decltype(glz::get<I>(tied))>;
            bc_executor<elem_t, RootT> child_exec(ex.bc_, glz::get<I>(tied), ex.root_value_, &ls, ex.out_);
            res = child_exec.execute_impl(pc + 1, body_end - 1);
          }()), ...);
        }(std::make_index_sequence<sz>{});
        return res;
      }
      return {};
    });
    if (!r) return std::unexpected(r.error());
    pc = body_end;
    return {};
  }

  static std::expected<void, error_ctx> handle_emit_inverted(bc_executor& ex, std::size_t& pc, std::string&) {
    auto const& instr = ex.bc_.instructions[pc];
    auto const& ref   = ex.bc_.var_refs[instr.operand2];
    bool empty = true;
    (void)ex.for_each_field(ex.value_, ref.key, ref.field_index, ref.has_dot, [&](auto const& field) {
      using FT = std::remove_cvref_t<decltype(field)>;
      if constexpr (ct_is_vector_like<FT>) { empty = field.empty(); }
      else if constexpr (std::same_as<FT, bool>) { empty = !field; }
      else if constexpr (is_std_optional_v<FT>) { empty = !field.has_value(); }
      else if constexpr (ct_is_map_like<FT>) { empty = field.empty(); }
      else if constexpr (ct_is_set_like<FT>) { empty = field.empty(); }
      else if constexpr (std::same_as<FT, std::string> || std::same_as<FT, std::string_view>) { empty = field.empty(); }
      else if constexpr (std::is_arithmetic_v<FT>) { empty = (field == 0); }
      else if constexpr (ct_glz_reflectable<FT>) { empty = false; }
    });
    if (empty) {
      auto body_end = instr.operand;
      if (body_end <= pc + 1 || body_end > ex.bc_.instructions.size())
        return std::unexpected(error_ctx{.position = pc, .ec = error_code::syntax_error});
      auto r = ex.execute_impl(pc + 1, body_end - 1);
      if (!r) return std::unexpected(r.error());
      pc = body_end;
    } else {
      pc = instr.operand;
    }
    return {};
  }

  static std::expected<void, error_ctx> handle_emit_if(bc_executor& ex, std::size_t& pc, std::string&) {
    auto const& instr = ex.bc_.instructions[pc];
    auto const& ref   = ex.bc_.var_refs[instr.operand2];
    bool cond = false;
    if (ref.key.starts_with("loop.")) {
      if (ex.loop_) {
        if (ref.key == "loop.is_last") { cond = (ex.loop_->index + 1 == ex.loop_->count); }
        else if (ref.key == "loop.is_first") { cond = (ex.loop_->index == 0); }
        else if (ref.key == "loop.index") { cond = (ex.loop_->index != 0); }
        else if (ref.key == "loop.key") { cond = !ex.loop_->key.empty(); }
      }
    } else {
      (void)ex.for_each_field(ex.value_, ref.key, ref.field_index, ref.has_dot, [&](auto const& field) {
        using FT = std::remove_cvref_t<decltype(field)>;
        if constexpr (std::same_as<FT, bool>) { cond = field; }
        else if constexpr (ct_is_vector_like<FT>) { cond = !field.empty(); }
        else if constexpr (std::same_as<FT, std::string> || std::same_as<FT, std::string_view>) { cond = !field.empty(); }
        else if constexpr (std::is_arithmetic_v<FT>) { cond = (field != 0); }
        else if constexpr (is_std_optional_v<FT>) { cond = field.has_value(); }
        else if constexpr (ct_is_map_like<FT>) { cond = !field.empty(); }
        else if constexpr (ct_is_set_like<FT>) { cond = !field.empty(); }
      });
    }
    if (!cond) { pc = instr.operand; } else { ++pc; }
    return {};
  }

  static std::expected<void, error_ctx> handle_emit_if_cmp(bc_executor& ex, std::size_t& pc, std::string&) {
    auto const& instr = ex.bc_.instructions[pc];
    bool is_eq = instr.op == bc_opcode::emit_if_eq;
    auto const& ref   = ex.bc_.var_refs[instr.operand2];
    int rhs = ref.int_filters.empty() ? 0 : ref.int_filters[0].arg;
    bool cond = false;
    (void)ex.for_each_field(ex.value_, ref.key, ref.field_index, ref.has_dot, [&](auto const& field) {
      using FT = std::remove_cvref_t<decltype(field)>;
      if constexpr (std::is_arithmetic_v<FT>) {
        cond = is_eq ? (static_cast<long long>(field) == static_cast<long long>(rhs))
                     : (static_cast<long long>(field) != static_cast<long long>(rhs));
      }
    });
    if (!cond) { pc = instr.operand; } else { ++pc; }
    return {};
  }

  static std::expected<void, error_ctx> handle_emit_else(bc_executor& ex, std::size_t& pc, std::string&) {
    pc = ex.bc_.instructions[pc].operand;
    return {};
  }

  static std::expected<void, error_ctx> handle_emit_endif(bc_executor&, std::size_t& pc, std::string&) {
    ++pc;
    return {};
  }

  static std::expected<void, error_ctx> handle_emit_at_section(bc_executor& ex, std::size_t& pc, std::string&) {
    auto const& instr = ex.bc_.instructions[pc];
    bool cond = false;
    if (ex.loop_) {
      auto kind = instr.operand2;
      if (kind == 0) { cond = ex.loop_->index > 0; }
      else if (kind == 1) { cond = ex.loop_->index == 0; }
      else if (kind == 2) { cond = ex.loop_->index + 1 == ex.loop_->count; }
    }
    if (cond) {
      auto body_end = instr.operand;
      auto r = ex.execute_impl(pc + 1, body_end - 1);
      if (!r) return std::unexpected(r.error());
      pc = body_end;
    } else {
      pc = instr.operand;
    }
    return {};
  }

  static std::expected<void, error_ctx> handle_emit_at_inverted(bc_executor& ex, std::size_t& pc, std::string&) {
    auto const& instr = ex.bc_.instructions[pc];
    bool cond = false;
    if (ex.loop_) {
      auto kind = instr.operand2;
      if (kind == 0) { cond = ex.loop_->index != 0; }
      else if (kind == 1) { cond = ex.loop_->index == 0; }
      else if (kind == 2) { cond = ex.loop_->index + 1 == ex.loop_->count; }
    }
    if (cond) {
      pc = instr.operand;
    } else {
      auto body_end = instr.operand;
      auto r = ex.execute_impl(pc + 1, body_end - 1);
      if (!r) return std::unexpected(r.error());
      pc = body_end;
    }
    return {};
  }

  static std::expected<void, error_ctx> handle_emit_if_filtered(bc_executor& ex, std::size_t& pc, std::string& filtered) {
    auto const& instr = ex.bc_.instructions[pc];
    bool cond = !filtered.empty() && filtered != "false" && filtered != "0";
    if (!cond) { pc = instr.operand; } else { ++pc; }
    return {};
  }

public:
  bc_executor(bytecode const& bc, T const& value, RootT const& root_value, bc_loop_state const* loop, std::string& out) : bc_(bc), value_(value), root_value_(root_value), loop_(loop), out_(out) {}

  /**
   * @brief バイトコードの実行を開始する
   * @return std::expected<void, error_ctx> 実行結果
   */
  std::expected<void, error_ctx> execute() { return execute_impl(0, bc_.instructions.size()); }

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
        &&L_emit_literal,            // 0
        &&L_emit_var,                // 1
        &&L_emit_var_raw,            // 2
        &&L_emit_section,            // 3
        &&L_emit_end,                // 4
        &&L_emit_inverted,           // 6
        &&L_emit_at_index,           // 7
        &&L_emit_at_first,           // 8
        &&L_emit_at_last,            // 9
        &&L_emit_if,                 // 10
        &&L_emit_if_eq,              // 11
        &&L_emit_if_ne,              // 12
        &&L_emit_else,               // 13
        &&L_emit_endif,              // 14
        &&L_emit_at_section,         // 15
        &&L_emit_at_inverted,        // 16
        &&L_emit_litvar,             // 17
        &&L_emit_litvar_raw,         // 18
        &&L_emit_at_root,            // 19
        &&L_emit_at_root_field,      // 20
        &&L_emit_at_root_field_raw,  // 21
        &&L_emit_at_key,             // 22
        &&L_emit_this,               // 23
        &&L_resolve_filtered,        // 24
        &&L_filter_upper,            // 25
        &&L_filter_lower,            // 26
        &&L_filter_capitalize,       // 27
        &&L_filter_title,            // 28
        &&L_filter_trim,             // 29
        &&L_filter_ltrim,            // 30
        &&L_filter_rtrim,            // 31
        &&L_filter_left,             // 32
        &&L_filter_right,            // 33
        &&L_filter_center,           // 34
        &&L_filter_truncate,         // 35
        &&L_filter_substr,           // 36
        &&L_filter_replace,          // 37
        &&L_emit_filtered,           // 38
        &&L_emit_filtered_raw,       // 39
        &&L_filter_int_abs,          // 40
        &&L_filter_int_hex,          // 41
        &&L_filter_int_oct,          // 42
        &&L_filter_int_bin,          // 43
        &&L_filter_int_neg,          // 44
        &&L_filter_int_mod,          // 45
        &&L_filter_int_numify,       // 46
        &&L_filter_int_is_neg,       // 47
        &&L_filter_int_eq,           // 48
        &&L_filter_int_ne,           // 49
        &&L_filter_int_gt,           // 50
        &&L_filter_int_gte,          // 51
        &&L_filter_int_lt,           // 52
        &&L_filter_int_lte,          // 53
        &&L_filter_int_zerofill,     // 54
        &&L_filter_int_add,          // 55
        &&L_filter_int_sub,          // 56
        &&L_filter_int_mul,          // 57
        &&L_filter_int_div,          // 58
        &&L_filter_float_precision,  // 59
        &&L_emit_if_filtered,        // 60
        &&L_emit_break,              // 61
        &&L_emit_continue,           // 62
        &&L_emit_at_index1,          // 63
        &&L_emit_at_size,            // 64
        &&L_emit_var_size,           // 65
        &&L_halt,                    // 66
    };

/** @brief 現在の命令のオペコードに対応するラベルにジャンプする */
#define DISPATCH() goto* dispatch_table[static_cast<int>(bc_.instructions[pc].op)]

    if (pc >= end)
      goto L_halt;
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
    bool        raw = (bc_.instructions[pc].op == bc_opcode::emit_var_raw);
    auto        r   = for_each_field(value_, ref.key, ref.field_index, ref.has_dot, [&](auto const& field) { emit_var_value(field, raw); });
    if (!r)
      return r;
    ++pc;
    DISPATCH();
  }

  /**
   * @brief セクションブロックの開始
   * @details 配列の場合はループして各要素を描画、bool の場合は真ならボディを描画。
   *          ループ時は bc_loop_state を生成して子 executor に渡す。
   */
  L_emit_section: {
    auto const& instr    = bc_.instructions[pc];
    auto const& ref      = bc_.var_refs[instr.operand2];
    auto        body_end = instr.operand;
    auto        else_pc  = instr.operand3;

    if (body_end <= pc + 1 || body_end > bc_.instructions.size()) {
      return std::unexpected(error_ctx{.position = pc, .ec = error_code::syntax_error});
    }

    bool is_falsy = true;
    auto r = for_each_field(value_, ref.key, ref.field_index, ref.has_dot, [&](auto const& field) -> std::expected<void, error_ctx> {
      using FT = std::remove_cvref_t<decltype(field)>;
      if constexpr (ct_is_vector_like<FT>) {
        if (!field.empty()) is_falsy = false;
        using elem_t = typename FT::value_type;
        bc_loop_state ls;
        ls.count = static_cast<std::uint32_t>(field.size());

        for (ls.index = 0; ls.index < static_cast<std::uint32_t>(field.size()); ++ls.index) {
          ls.continue_flag = false;
          bc_executor<elem_t, RootT> child_exec(bc_, field[ls.index], root_value_, &ls, out_);
          auto                       r2 = child_exec.execute_impl(pc + 1, body_end - 1);
          if (!r2)
            return r2;
          if (ls.continue_flag) {
            ls.continue_flag = false;
            continue;
          }
          if (ls.break_flag)
            break;
        }
      } else if constexpr (std::same_as<FT, bool>) {
        is_falsy = !field;
        if (field) {
          auto r2 = execute_impl(pc + 1, body_end - 1);
          if (!r2)
            return r2;
        }
      } else if constexpr (is_std_optional_v<FT>) {
        is_falsy = !field.has_value();
        if (field.has_value()) {
          using inner_t = typename FT::value_type;
          bc_executor<inner_t, RootT> child_exec(bc_, *field, root_value_, nullptr, out_);
          auto                        r2 = child_exec.execute_impl(pc + 1, body_end - 1);
          if (!r2)
            return r2;
        }
      } else if constexpr (ct_is_map_like<FT>) {
        if (!field.empty()) is_falsy = false;
        bc_loop_state ls;
        ls.count = static_cast<std::uint32_t>(field.size());
        for (auto const& [k, v] : field) {
          ls.key      = std::string_view{k};
          using val_t = std::remove_cvref_t<decltype(v)>;
          bc_executor<val_t, RootT> child_exec(bc_, v, root_value_, &ls, out_);
          auto                      r2 = child_exec.execute_impl(pc + 1, body_end - 1);
          if (!r2)
            return r2;
          if (ls.break_flag)
            break;
          ++ls.index;
        }
      } else if constexpr (ct_is_set_like<FT>) {
        if (!field.empty()) is_falsy = false;
        using elem_t = typename FT::value_type;
        bc_loop_state ls;
        ls.count = static_cast<std::uint32_t>(field.size());
        for (auto const& elem : field) {
          ls.continue_flag = false;
          bc_executor<elem_t, RootT> child_exec(bc_, elem, root_value_, &ls, out_);
          auto                       r2 = child_exec.execute_impl(pc + 1, body_end - 1);
          if (!r2)
            return r2;
          if (ls.continue_flag) {
            ls.continue_flag = false;
            continue;
          }
          if (ls.break_flag)
            break;
          ++ls.index;
        }
      } else if constexpr (ct_glz_reflectable<FT>) {
        is_falsy = false;
        constexpr auto                 sz   = glz::reflect<FT>::size;
        auto                           tied = glz::to_tie(field);
        std::expected<void, error_ctx> res{};
        [&]<std::size_t... I>(std::index_sequence<I...>) {
          (([&] {
             if (!res)
               return;
             bc_loop_state ls;
             ls.count     = sz;
             ls.index     = I;
             ls.key       = glz::reflect<FT>::keys[I];
             using elem_t = std::remove_cvref_t<decltype(glz::get<I>(tied))>;
             bc_executor<elem_t, RootT> child_exec(bc_, glz::get<I>(tied), root_value_, &ls, out_);
             res = child_exec.execute_impl(pc + 1, body_end - 1);
           }()),
           ...);
        }(std::make_index_sequence<sz>{});
        return res;
      }
      return {};
    });
    if (!r)
      return r;

    if (else_pc > 0 && is_falsy) {
      pc = else_pc;
    } else {
      pc = body_end;
    }
    DISPATCH();
  }

  /** @brief 実行終端（通常到達しない） */
  L_emit_end: { return {}; }

  /**
   * @brief 逆セクションの開始
   * @details 配列が空または bool が偽の場合にボディを描画する。
   *          条件が成立しなければ operand の位置（endif）にジャンプする。
   */
  L_emit_inverted: {
    auto const& instr = bc_.instructions[pc];
    auto const& ref   = bc_.var_refs[instr.operand2];
    auto        else_pc = instr.operand3;
    bool        empty = true;
    (void)for_each_field(value_, ref.key, ref.field_index, ref.has_dot, [&](auto const& field) {
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
      } else if constexpr (std::same_as<FT, std::string> || std::same_as<FT, std::string_view>) {
        empty = field.empty();
      } else if constexpr (std::is_arithmetic_v<FT>) {
        empty = (field == 0);
      } else if constexpr (ct_glz_reflectable<FT>) {
        empty = false;
      }
    });
    auto body_end = instr.operand;
    if (body_end <= pc + 1 || body_end > bc_.instructions.size()) {
      return std::unexpected(error_ctx{.position = pc, .ec = error_code::syntax_error});
    }
    if (empty) {
      auto r = execute_impl(pc + 1, body_end - 1);
      if (!r)
        return r;
      pc = body_end;
    } else if (else_pc > 0) {
      auto r = execute_impl(else_pc, body_end - 1);
      if (!r)
        return r;
      pc = body_end;
    } else {
      pc = body_end;
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

  /** @brief ループの @index1 を 1 始まりの数値として出力する */
  L_emit_at_index1: {
    if (loop_) {
      std::array<char, 16> buf;
      auto [ptr, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), loop_->index + 1);
      if (ec == std::errc{}) {
        out_.append(buf.data(), ptr);
      }
    }
    ++pc;
    DISPATCH();
  }

  /** @brief ループの @size を総要素数として出力する */
  L_emit_at_size: {
    if (loop_) {
      std::array<char, 16> buf;
      auto [ptr, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), loop_->count);
      if (ec == std::errc{}) {
        out_.append(buf.data(), ptr);
      }
    }
    ++pc;
    DISPATCH();
  }

  /** @brief 変数の要素数を出力する ({{field.size}}) */
  L_emit_var_size: {
    auto const& ref = bc_.var_refs[bc_.instructions[pc].operand];
    auto r = for_each_field(value_, ref.key, ref.field_index, ref.has_dot, [&](auto const& field) {
      using FT = std::remove_cvref_t<decltype(field)>;
      std::size_t sz = 0;
      if constexpr (ct_is_vector_like<FT>) {
        sz = field.size();
      } else if constexpr (ct_is_map_like<FT>) {
        sz = field.size();
      } else if constexpr (ct_is_set_like<FT>) {
        sz = field.size();
      }
      std::array<char, 16> buf;
      auto [ptr, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), sz);
      if (ec == std::errc{}) {
        out_.append(buf.data(), ptr);
      }
    });
    if (!r) return r;
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
    auto const& ref   = bc_.var_refs[instr.operand2];
    bool        cond  = false;

    /** loop.is_last, loop.is_first, loop.index の場合は loop 変数を直接参照 */
    if (ref.key.starts_with("loop.")) {
      if (loop_) {
        if (ref.key == "loop.is_last") {
          cond = (loop_->index + 1 == loop_->count);
        } else if (ref.key == "loop.is_first") {
          cond = (loop_->index == 0);
        } else if (ref.key == "loop.index") {
          /** loop.index は 0 以外で真（0 は偽扱い） */
          cond = (loop_->index != 0);
        } else if (ref.key == "loop.key") {
          /** loop.key は空でなければ真 */
          cond = !loop_->key.empty();
        }
      }
    } else {
      /** 通常のフィールド参照による truthiness 判定 */
      (void)for_each_field(value_, ref.key, ref.field_index, ref.has_dot, [&](auto const& field) {
        using FT = std::remove_cvref_t<decltype(field)>;
        if constexpr (std::same_as<FT, bool>) {
          cond = field;
        } else if constexpr (ct_is_vector_like<FT>) {
          cond = !field.empty();
        } else if constexpr (std::same_as<FT, std::string> || std::same_as<FT, std::string_view>) {
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

  /** @brief if (var == int_literal): 整数フィールドがオペランド値と等しいとき真 */
  L_emit_if_eq: {
    auto const& instr = bc_.instructions[pc];
    auto const& ref   = bc_.var_refs[instr.operand2];
    int rhs = 0;
    if (!ref.int_filters.empty()) {
      rhs = ref.int_filters[0].arg;
    }
    bool cond = false;
    (void)for_each_field(value_, ref.key, ref.field_index, ref.has_dot, [&](auto const& field) {
      using FT = std::remove_cvref_t<decltype(field)>;
      if constexpr (std::is_arithmetic_v<FT>) {
        cond = (static_cast<long long>(field) == static_cast<long long>(rhs));
      }
    });
    if (!cond) {
      pc = instr.operand;
    } else {
      ++pc;
    }
    DISPATCH();
  }

  /** @brief if (var != int_literal): 整数フィールドがオペランド値と異なるとき真 */
  L_emit_if_ne: {
    auto const& instr = bc_.instructions[pc];
    auto const& ref   = bc_.var_refs[instr.operand2];
    int rhs = 0;
    if (!ref.int_filters.empty()) {
      rhs = ref.int_filters[0].arg;
    }
    bool cond = false;
    (void)for_each_field(value_, ref.key, ref.field_index, ref.has_dot, [&](auto const& field) {
      using FT = std::remove_cvref_t<decltype(field)>;
      if constexpr (std::is_arithmetic_v<FT>) {
        cond = (static_cast<long long>(field) != static_cast<long long>(rhs));
      }
    });
    if (!cond) {
      pc = instr.operand;
    } else {
      ++pc;
    }
    DISPATCH();
  }

  /** @brief else ブランチ: operand に設定されたジャンプ先に移動する */
  L_emit_else: {
    auto const& instr = bc_.instructions[pc];
    pc                = instr.operand;
    DISPATCH();
  }

  /** @brief endif: if ブロック終端、次の命令に進む */
  L_emit_endif: {
    ++pc;
    DISPATCH();
  }

  /** @brief @var セクション: @index/@first/@last に基づく条件付き描画 */
  L_emit_at_section: {
    auto const& instr = bc_.instructions[pc];
    bool        cond  = false;
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
      auto r        = execute_impl(pc + 1, body_end - 1);
      if (!r)
        return r;
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
    bool        cond  = false;
    if (loop_) {
      auto kind = instr.operand2;
      if (kind == 0) {
        /** @index: index != 0 のとき逆セクションをスキップ（index == 0 で描画） */
        cond = loop_->index != 0;
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
      auto r        = execute_impl(pc + 1, body_end - 1);
      if (!r)
        return r;
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
    bool        raw = (instr.op == bc_opcode::emit_litvar_raw);
    auto        r   = for_each_field(value_, ref.key, ref.field_index, ref.has_dot, [&](auto const& field) { emit_var_value(field, raw); });
    if (!r)
      return r;
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
    bool        raw = (bc_.instructions[pc].op == bc_opcode::emit_at_root_field_raw);
    auto        r   = for_each_field(root_value_, ref.key, ref.field_index, ref.has_dot, [&](auto const& field) { emit_var_value(field, raw); });
    if (!r)
      return r;
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
    this->emit_this_scratch_.clear();
    if constexpr (serializable_v<T>) {
      serialize_value(this->emit_this_scratch_, value_);
    } else if constexpr (ct_glz_reflectable<T> && glz::write_supported<T, glz::JSON>) {
      if (auto ec = glz::write_json(value_, this->emit_this_scratch_)) {
        return std::unexpected(error_ctx{.position = pc, .ec = error_code::syntax_error});
      }
    }
    html_escape_into(out_, this->emit_this_scratch_);
    ++pc;
    DISPATCH();
  }

  /** @brief フィルタ付き変数解決（フィルタを一括適用し個別命令をスキップ） */
  L_resolve_filtered: {
    auto const& instr   = bc_.instructions[pc];
    auto const& var_ref = bc_.var_refs[instr.operand2];
    filtered_value_.clear();
    auto r = for_each_field(value_, var_ref.key, var_ref.field_index, var_ref.has_dot, [&](auto const& field) {
      using FT = std::remove_cvref_t<decltype(field)>;
      if constexpr (serializable_v<FT>) {
        serialize_value(filtered_value_, field);
      } else if constexpr (is_std_optional_v<FT>) {
        if (field.has_value()) {
          serialize_value(filtered_value_, *field);
        }
      }
    });
    if (!r)
      return r;
    // フィルタを一括適用（dispatch 削減のため）
    for (auto const& f : var_ref.filters)
      apply_string_filter(filtered_value_, f);
    for (auto const& f : var_ref.int_filters) {
      if (auto err = apply_int_filter(filtered_value_, f); !err)
        return std::unexpected(err.error());
    }
    for (auto const& f : var_ref.float_filters)
      apply_float_filter(filtered_value_, f);
    // 個別フィルタ命令をスキップ
    ++pc;
    pc += instr.operand;
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

  /** @brief 部分文字列置換（デフォルト: 改行→空白） */
  L_filter_replace: {
    apply_string_filter(filtered_value_, {.filter = string_filter::replace});
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
  L_filter_int_add: {
    if (auto err = apply_int_filter(filtered_value_, {.filter = int_filter::add, .arg = static_cast<int>(bc_.instructions[pc].operand)}); !err) {
      return std::unexpected(err.error());
    }
    ++pc;
    DISPATCH();
  }
  L_filter_int_sub: {
    if (auto err = apply_int_filter(filtered_value_, {.filter = int_filter::sub, .arg = static_cast<int>(bc_.instructions[pc].operand)}); !err) {
      return std::unexpected(err.error());
    }
    ++pc;
    DISPATCH();
  }
  L_filter_int_mul: {
    if (auto err = apply_int_filter(filtered_value_, {.filter = int_filter::mul, .arg = static_cast<int>(bc_.instructions[pc].operand)}); !err) {
      return std::unexpected(err.error());
    }
    ++pc;
    DISPATCH();
  }
  L_filter_int_div: {
    if (auto err = apply_int_filter(filtered_value_, {.filter = int_filter::div, .arg = static_cast<int>(bc_.instructions[pc].operand)}); !err) {
      return std::unexpected(err.error());
    }
    ++pc;
    DISPATCH();
  }
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

  /** @brief 不等価判定: 値と引数が異なれば "true"、そうでなければ "false" を出力 */
  L_filter_int_ne: {
    if (auto err = apply_int_filter(filtered_value_, {.filter = int_filter::ne, .arg = static_cast<int>(bc_.instructions[pc].operand)}); !err) {
      return std::unexpected(err.error());
    }
    ++pc;
    DISPATCH();
  }

  /** @brief 大なり判定: 値が引数より大きければ "true"、そうでなければ "false" を出力 */
  L_filter_int_gt: {
    if (auto err = apply_int_filter(filtered_value_, {.filter = int_filter::gt, .arg = static_cast<int>(bc_.instructions[pc].operand)}); !err) {
      return std::unexpected(err.error());
    }
    ++pc;
    DISPATCH();
  }

  /** @brief 以上判定: 値が引数以上なら "true"、そうでなければ "false" を出力 */
  L_filter_int_gte: {
    if (auto err = apply_int_filter(filtered_value_, {.filter = int_filter::gte, .arg = static_cast<int>(bc_.instructions[pc].operand)}); !err) {
      return std::unexpected(err.error());
    }
    ++pc;
    DISPATCH();
  }

  /** @brief 小なり判定: 値が引数未満なら "true"、そうでなければ "false" を出力 */
  L_filter_int_lt: {
    if (auto err = apply_int_filter(filtered_value_, {.filter = int_filter::lt, .arg = static_cast<int>(bc_.instructions[pc].operand)}); !err) {
      return std::unexpected(err.error());
    }
    ++pc;
    DISPATCH();
  }

  /** @brief 以下判定: 値が引数以下なら "true"、そうでなければ "false" を出力 */
  L_filter_int_lte: {
    if (auto err = apply_int_filter(filtered_value_, {.filter = int_filter::lte, .arg = static_cast<int>(bc_.instructions[pc].operand)}); !err) {
      return std::unexpected(err.error());
    }
    ++pc;
    DISPATCH();
  }

  /** @brief フィルタ適用済み値での if 分岐 */
  L_emit_if_filtered: {
    auto const& instr = bc_.instructions[pc];
    bool        cond  = !filtered_value_.empty() && filtered_value_ != "false" && filtered_value_ != "0";
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
  L_halt: { return {}; }

#undef DISPATCH

#else
    using handler_fn = std::expected<void, error_ctx> (*)(bc_executor&, std::size_t&, std::string&);
    static constexpr handler_fn dispatch_table[] = {
      &handle_emit_literal,       // 0
      &handle_emit_var,           // 1
      &handle_emit_var,           // 2 emit_var_raw
      &handle_emit_section,       // 3
      &handle_emit_end,           // 4
      &handle_emit_inverted,      // 5
      &handle_emit_at_index,      // 6
      &handle_emit_at_first,      // 7
      &handle_emit_at_last,       // 8
      &handle_emit_if,            // 9
      &handle_emit_if_cmp,        // 10 emit_if_eq
      &handle_emit_if_cmp,        // 11 emit_if_ne
      &handle_emit_else,          // 12
      &handle_emit_endif,         // 13
      &handle_emit_at_section,    // 14
      &handle_emit_at_inverted,   // 15
      &handle_emit_litvar,        // 16
      &handle_emit_litvar,        // 17 emit_litvar_raw
      &handle_emit_at_root,       // 18
      &handle_emit_at_root_field, // 19
      &handle_emit_at_root_field, // 20 emit_at_root_field_raw
      &handle_emit_at_key,        // 21
      &handle_emit_this,          // 22
      &handle_resolve_filtered,   // 23
      &handle_string_filter,      // 24 filter_upper
      &handle_string_filter,      // 25 filter_lower
      &handle_string_filter,      // 26 filter_capitalize
      &handle_string_filter,      // 27 filter_title
      &handle_string_filter,      // 28 filter_trim
      &handle_string_filter,      // 29 filter_ltrim
      &handle_string_filter,      // 30 filter_rtrim
      &handle_string_filter_arg,  // 31 filter_left
      &handle_string_filter_arg,  // 32 filter_right
      &handle_string_filter_arg,  // 33 filter_center
      &handle_string_filter_arg,  // 34 filter_truncate
      &handle_string_filter_arg2, // 35 filter_substr
      &handle_string_filter,      // 36 filter_replace
      &handle_emit_filtered,      // 37
      &handle_emit_filtered,      // 38 emit_filtered_raw
      &handle_int_filter,         // 39 filter_int_abs
      &handle_int_filter,         // 40 filter_int_hex
      &handle_int_filter,         // 41 filter_int_oct
      &handle_int_filter,         // 42 filter_int_bin
      &handle_int_filter,         // 43 filter_int_neg
      &handle_int_filter,         // 44 filter_int_mod
      &handle_int_filter,         // 45 filter_int_numify
      &handle_int_filter,         // 46 filter_int_is_neg
      &handle_int_filter,         // 47 filter_int_eq
      &handle_int_filter,         // 48 filter_int_ne
      &handle_int_filter,         // 49 filter_int_gt
      &handle_int_filter,         // 50 filter_int_gte
      &handle_int_filter,         // 51 filter_int_lt
      &handle_int_filter,         // 52 filter_int_lte
      &handle_int_filter,         // 53 filter_int_zerofill
      &handle_int_filter,         // 54 filter_int_add
      &handle_int_filter,         // 55 filter_int_sub
      &handle_int_filter,         // 56 filter_int_mul
      &handle_int_filter,         // 57 filter_int_div
      &handle_float_filter,       // 58 filter_float_precision
      &handle_emit_if_filtered,   // 59
      &handle_emit_break,         // 60
      &handle_emit_continue,      // 61
      &handle_emit_at_index1,     // 62
      &handle_emit_at_size,       // 63
      &handle_emit_var_size,      // 64
      &handle_emit_halt,          // 65
    };

    while (pc < end) {
      auto op = static_cast<int>(bc_.instructions[pc].op);
      if (op < 0 || op >= static_cast<int>(std::size(dispatch_table)))
        return std::unexpected(error_ctx{.position = pc, .ec = error_code::syntax_error});
      auto r = dispatch_table[op](*this, pc, filtered_value_);
      if (!r) return std::unexpected(r.error());
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
  auto        estimated = bc.literal_total_size > 32 ? bc.literal_total_size * 4 : 256;
  out.reserve(estimated);
  bc_executor<T> exec(bc, value, value, nullptr, out);
  auto           r = exec.execute();
  if (!r) {
    return std::unexpected(r.error());
  }
  return out;
}

/**
 * @brief バイトコードを実行して既存の出力バッファに追記する（バッファ再利用用）
 * @tparam T コンテキスト値の型
 * @param bc バイトコード
 * @param value コンテキスト値
 * @param out 出力先バッファ（内容はクリアされる）
 * @return std::expected<void, error_ctx> 実行結果
 */
template <class T>
std::expected<void, error_ctx> bc_execute_into(bytecode const& bc, T const& value, std::string& out) {
  out.clear();
  auto estimated = bc.literal_total_size > 64 ? bc.literal_total_size * 4 : 256;
  if (out.capacity() < estimated)
    out.reserve(estimated);
  bc_executor<T> exec(bc, value, value, nullptr, out);
  return exec.execute();
}

}  // namespace injamm::detail
