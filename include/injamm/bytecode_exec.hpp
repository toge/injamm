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

#ifndef INJAMM_FAST_PATH
#define INJAMM_FAST_PATH 1
#endif

#include "../injamm.hpp"
#include "bytecode.hpp"
#include "enum_io.hpp"
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
  std::string_view binding_name{};
  /**< 現在ループのセクションキー名（ループ内で配列名＝現在要素として束縛） */
  void const* binding_elem = nullptr;
  /**< 現在要素へのポインタ（ループ内束縛用） */
  bool (*binding_resolve)(std::string&, std::string_view, bool, void const*, std::string_view) = nullptr;
  /**< 現在要素を key に従って出力する型消去リゾルバ */
  bool (*binding_truthy)(void const*, std::string_view) = nullptr;
  /**< 現在要素の真偽を評価する型消去リゾルバ */
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
   * @brief 末端ノードの処理：パス全体をキーとしてフィールドを探索しvisitorを適用
   */
  template <class V, class F>
  static auto process_terminal_node(V const& v, std::string_view path, F&& visitor) -> std::expected<void, error_ctx> {
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

  /**
   * @brief 配列インデックスアクセスの処理
   */
  template <class FT, class F>
  static auto process_vector_index_access(FT const& field, std::string_view rest_path, std::size_t idx_dot, std::string_view idx_str, F&& visitor) -> std::expected<void, error_ctx> {
    if (!idx_str.empty() && idx_str.find_first_not_of("0123456789") == std::string_view::npos) {
      std::size_t idx = 0;
      auto [ptr, ec] = std::from_chars(idx_str.data(), idx_str.data() + idx_str.size(), idx);
      if (ec == std::errc{} && idx < field.size()) {
        auto const& elem = field[idx];
        using ET = std::remove_cvref_t<decltype(elem)>;
        using visitor_r = decltype(visitor(elem));
        
        if (idx_dot == std::string_view::npos) {
          if constexpr (std::same_as<visitor_r, void>) {
            visitor(elem);
            return {};
          } else {
            return visitor(elem);
          }
        } else if constexpr (ct_glz_reflectable<ET>) {
          return resolve_nested_path(elem, rest_path.substr(idx_dot + 1), std::forward<F>(visitor));
        }
      }
    }
    return {};
  }

  /**
   * @brief 中間ノードの処理：最初のキーでフィールドを検索し、残りのパスで再帰
   */
  template <class V, class F>
  static auto process_intermediate_node(V const& v, std::string_view first_key, std::string_view rest_path, F&& visitor) -> std::expected<void, error_ctx> {
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
                 (void)process_vector_index_access(field, rest_path, idx_dot, idx_str, std::forward<F>(visitor));
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
                 result = process_vector_index_access(field, rest_path, idx_dot, idx_str, std::forward<F>(visitor));
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
static auto resolve_nested_path(V const& v, std::string_view path, F&& visitor) -> std::expected<void, error_ctx> {
  /** ドットの位置で分割: 末端に達したか判定 */
  auto dot_pos = path.find('.');
  if (dot_pos == std::string_view::npos) {
    /** 末端: パス全体をキーとして一致するフィールドを探し visitor を適用 */
    return process_terminal_node(v, path, std::forward<F>(visitor));
  }

  /** 中間ノード: 最初のキーを取得し、残りのパスで再帰 */
  auto first_key = path.substr(0, dot_pos);
  auto rest_path = path.substr(dot_pos + 1);
  return process_intermediate_node(v, first_key, rest_path, std::forward<F>(visitor));
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
static auto for_each_field(V const& v, std::string_view key, std::uint32_t field_index, bool has_dot, F&& visitor) -> std::expected<void, error_ctx> {
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
  /** @brief フィールド値を指定バッファに追記する（束縛リゾルバ兼用の静的版） */
  static void emit_value_static(std::string& out, auto const& field, bool raw) {
    using FT = std::remove_cvref_t<decltype(field)>;
    if constexpr (std::same_as<FT, bool>) {
      if (field) {
        out.append("true", 4);
      } else {
        out.append("false", 5);
      }
    } else if constexpr (std::same_as<FT, std::string> || std::same_as<FT, std::string_view>) {
      if (raw) {
        out.append(field.data(), field.size());
      } else {
        html_escape_into(out, field);
      }
    } else if constexpr (std::is_enum_v<FT>) {
      serialize_enum(out, field, raw);
    } else if constexpr (is_chrono_time_point_v<FT>) {
      serialize_chrono(out, field);
    } else if constexpr (std::is_arithmetic_v<FT> && !std::same_as<FT, bool>) {
      if constexpr (std::floating_point<FT>) {
        std::array<char, glz::zmij::double_buffer_size> buf;
        auto end = glz::to_chars(buf.data(), field);
        out.append(buf.data(), static_cast<std::size_t>(end - buf.data()));
      } else {
        std::array<char, 32> buf;
        auto [ptr, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), field);
        if (ec == std::errc{}) {
          auto const n = static_cast<std::size_t>(ptr - buf.data());
          out.append(buf.data(), n);
        }
      }
    } else if constexpr (is_std_optional_v<FT>) {
      if (field.has_value()) {
        emit_value_static(out, *field, raw);
      }
    }
  }

  void emit_var_value(auto const& field, bool raw) { emit_value_static(out_, field, raw); }

  /** @brief loop.parent.* 変数の解決。解決できれば true を返す */
  static auto resolve_loop_parent_var(bc_executor const& ex, std::string_view key, bool raw) -> bool {
    // 呼び出し側で ref.is_loop_parent により事前ゲート済み。ここでは starts_with を省略。
    if (!ex.loop_) return false;
    if (!key.starts_with("loop.parent.")) return false;
    auto parent = ex.loop_->parent;
    if (!parent) return false;
    auto prop = key.substr(12);
    if (prop == "index") {
      std::array<char, 16> buf;
      auto [ptr, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), parent->index);
      if (ec == std::errc{}) ex.out_.append(buf.data(), static_cast<std::size_t>(ptr - buf.data()));
      return true;
    }
    if (prop == "index1") {
      std::array<char, 16> buf;
      auto [ptr, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), parent->index + 1);
      if (ec == std::errc{}) ex.out_.append(buf.data(), static_cast<std::size_t>(ptr - buf.data()));
      return true;
    }
    if (prop == "size") {
      std::array<char, 16> buf;
      auto [ptr, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), parent->count);
      if (ec == std::errc{}) ex.out_.append(buf.data(), static_cast<std::size_t>(ptr - buf.data()));
      return true;
    }
    if (prop == "is_first") {
      ex.out_.append(parent->index == 0 ? "true" : "false");
      return true;
    }
    if (prop == "is_last") {
      ex.out_.append((parent->index + 1 == parent->count) ? "true" : "false");
      return true;
    }
    if (prop == "key") {
      if (!parent->key.empty()) {
        if (raw) { ex.out_.append(parent->key); } else { html_escape_into(ex.out_, parent->key); }
      }
      return true;
    }
    return false;
  }

  /** @brief loop.parent.* の truthiness を判定。判定できれば true を返す */
  static auto eval_loop_parent_truthy(bc_executor const& ex, std::string_view key, bool& result) -> bool {
    if (!ex.loop_) return false;
    if (!key.starts_with("loop.parent.")) return false;
    auto parent = ex.loop_->parent;
    if (!parent) return false;
    auto prop = key.substr(12);
    if (prop == "index") { result = (parent->index != 0); return true; }
    if (prop == "index1") { result = true; return true; }
    if (prop == "size") { result = (parent->count != 0); return true; }
    if (prop == "is_first") { result = (parent->index == 0); return true; }
    if (prop == "is_last") { result = (parent->index + 1 == parent->count); return true; }
    if (prop == "key") { result = !parent->key.empty(); return true; }
    return false;
  }

  /** @brief ループ内束縛の型消去リゾルバ: 現在要素を key に従って出力する */
  template <class ElemT>
  static bool resolve_binding_var(std::string& out, std::string_view key, bool raw, void const* elem, std::string_view binding_name) {
    std::string_view sub = (key == binding_name)
                               ? std::string_view{}
                               : std::string_view{key.data() + binding_name.size() + 1,
                                                  key.size() - binding_name.size() - 1};
    auto const& e = *static_cast<ElemT const*>(elem);
    if constexpr (ct_glz_reflectable<ElemT>) {
      if (sub.empty()) {
        if constexpr (serializable_v<ElemT>) serialize_value(out, e);
        return true;
      }
      bool found = false;
      (void)for_each_field(e, sub, UINT32_MAX, sub.find('.') != std::string_view::npos,
        [&](auto const& f) { emit_value_static(out, f, raw); found = true; });
      return found;
    } else {
      if (!sub.empty()) return false;
      emit_value_static(out, e, raw);
      return true;
    }
  }

  /** @brief ループ内束縛の型消去リゾルバ: 現在要素の真偽を評価する */
  template <class ElemT>
  static bool eval_binding_truthy(void const* elem, std::string_view sub) {
    auto const& e = *static_cast<ElemT const*>(elem);
    if constexpr (ct_glz_reflectable<ElemT>) {
      if (!sub.empty()) {
        bool res = false;
        (void)for_each_field(e, sub, UINT32_MAX, sub.find('.') != std::string_view::npos, [&](auto const& f) {
          using FT = std::remove_cvref_t<decltype(f)>;
          if constexpr (std::same_as<FT, bool>) res = f;
          else if constexpr (ct_is_vector_like<FT>) res = !f.empty();
          else if constexpr (std::same_as<FT, std::string> || std::same_as<FT, std::string_view>) res = !f.empty();
          else if constexpr (std::is_arithmetic_v<FT>) res = (f != 0);
          else if constexpr (std::is_enum_v<FT>) res = (static_cast<std::underlying_type_t<FT>>(f) != 0);
          else if constexpr (is_std_optional_v<FT>) res = f.has_value();
          else if constexpr (ct_is_map_like<FT> || ct_is_set_like<FT>) res = !f.empty();
        });
        return res;
      }
      return true;
    } else {
      if (!sub.empty()) return false;
      if constexpr (std::same_as<ElemT, bool>) return e;
      else if constexpr (ct_is_vector_like<ElemT>) return !e.empty();
      else if constexpr (std::same_as<ElemT, std::string> || std::same_as<ElemT, std::string_view>) return !e.empty();
      else if constexpr (std::is_arithmetic_v<ElemT>) return (e != 0);
      else if constexpr (std::is_enum_v<ElemT>) return (static_cast<std::underlying_type_t<ElemT>>(e) != 0);
      else if constexpr (is_std_optional_v<ElemT>) return e.has_value();
      else if constexpr (ct_is_map_like<ElemT> || ct_is_set_like<ElemT>) return !e.empty();
      else return true;
    }
  }

  /** @brief ループスコープの束縛を探索し、key が配列名に一致すれば現在要素を出力する */
  static bool try_resolve_loop_binding(bc_executor const& ex, bc_var_ref const& ref, bool raw) {
    for (auto* lp = ex.loop_; lp; lp = lp->parent) {
      if (!lp->binding_resolve) continue;
      if (ref.key == lp->binding_name ||
          (ref.key.starts_with(lp->binding_name) && ref.key[lp->binding_name.size()] == '.')) {
        return lp->binding_resolve(ex.out_, ref.key, raw, lp->binding_elem, lp->binding_name);
      }
    }
    return false;
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
    if (ref.is_loop_parent && resolve_loop_parent_var(ex, ref.key, raw)) { ++pc; return {}; }
    bool        found = false;
    auto        r = for_each_field(ex.value_, ref.key, ref.field_index, ref.has_dot, [&](auto const& field) { found = true; ex.emit_var_value(field, raw); });
    if (!r) return std::unexpected(r.error());
    if (found) { ++pc; return {}; }
    if (try_resolve_loop_binding(ex, ref, raw)) { ++pc; return {}; }
    ++pc;
    return {};
  }

  static std::expected<void, error_ctx> handle_emit_litvar(bc_executor& ex, std::size_t& pc, std::string&) {
    bool raw = ex.bc_.instructions[pc].op == bc_opcode::emit_litvar_raw;
    ex.out_.append(ex.bc_.literals[ex.bc_.instructions[pc].operand]);
    auto const& ref = ex.bc_.var_refs[ex.bc_.instructions[pc].operand2];
    if (ref.is_loop_parent && resolve_loop_parent_var(ex, ref.key, raw)) { ++pc; return {}; }
    bool        found = false;
    auto        r = for_each_field(ex.value_, ref.key, ref.field_index, ref.has_dot, [&](auto const& field) { found = true; ex.emit_var_value(field, raw); });
    if (!r) return std::unexpected(r.error());
    if (found) { ++pc; return {}; }
    if (try_resolve_loop_binding(ex, ref, raw)) { ++pc; return {}; }
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
    auto r = for_each_field(ex.root_value_, ref.key, ref.field_index, ref.has_dot, [&](auto const& field) { ex.emit_var_value(field, raw); });
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
    auto r = for_each_field(ex.value_, ref.key, ref.field_index, ref.has_dot, [&](auto const& field) {
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
        ex.out_.append(buf.data(), static_cast<std::size_t>(ptr - buf.data()));
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
      if (ec == std::errc{}) ex.out_.append(buf.data(), static_cast<std::size_t>(ptr - buf.data()));
    }
    ++pc;
    return {};
  }

  static std::expected<void, error_ctx> handle_emit_at_index1(bc_executor& ex, std::size_t& pc, std::string&) {
    if (ex.loop_) {
      std::array<char, 16> buf;
      auto [ptr, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), ex.loop_->index + 1);
      if (ec == std::errc{}) ex.out_.append(buf.data(), static_cast<std::size_t>(ptr - buf.data()));
    }
    ++pc;
    return {};
  }

  static std::expected<void, error_ctx> handle_emit_at_size(bc_executor& ex, std::size_t& pc, std::string&) {
    if (ex.loop_) {
      std::array<char, 16> buf;
      auto [ptr, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), ex.loop_->count);
      if (ec == std::errc{}) ex.out_.append(buf.data(), static_cast<std::size_t>(ptr - buf.data()));
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
        if (ec == std::errc{}) ex.out_.append(buf.data(), static_cast<std::size_t>(ptr - buf.data()));
      }
    }
    ++pc;
    return {};
  }

  /** @brief ディスパッチテーブル用: filter_upper / lower / capitalize / title / trim / ltrim / rtrim / replace / default */
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
    case bc_opcode::filter_default: {
      auto def_str = ex.bc_.instructions[pc].operand < ex.bc_.literals.size()
        ? std::string_view{ex.bc_.literals[ex.bc_.instructions[pc].operand]}
        : std::string_view{};
      apply_string_filter(filtered, {.filter = string_filter::default_value, .str_arg1 = def_str});
      break;
    }
    default: return std::unexpected(error_ctx{.position = pc, .ec = error_code::syntax_error});
    }
    ++pc;
    return {};
  }

  /** @brief ディスパッチテーブル用: filter_left / right / center / truncate / indent（1引数） */
  static std::expected<void, error_ctx> handle_string_filter_arg(bc_executor& ex, std::size_t& pc, std::string& filtered) {
    auto op  = ex.bc_.instructions[pc].op;
    auto arg = static_cast<int>(ex.bc_.instructions[pc].operand);
    switch (op) {
    case bc_opcode::filter_left:     apply_string_filter(filtered, {.filter = string_filter::left, .arg1 = arg}); break;
    case bc_opcode::filter_right:    apply_string_filter(filtered, {.filter = string_filter::right, .arg1 = arg}); break;
    case bc_opcode::filter_center:   apply_string_filter(filtered, {.filter = string_filter::center, .arg1 = arg}); break;
    case bc_opcode::filter_truncate: apply_string_filter(filtered, {.filter = string_filter::truncate, .arg1 = arg}); break;
    case bc_opcode::filter_indent:   apply_string_filter(filtered, {.filter = string_filter::indent, .arg1 = arg}); break;
    default: return std::unexpected(error_ctx{.position = pc, .ec = error_code::syntax_error});
    }
    ++pc;
    return {};
  }

  /** @brief ディスパッチテーブル用: no-op（json, safe 等） */
  static std::expected<void, error_ctx> handle_noop(bc_executor&, std::size_t& pc, std::string&) {
    ++pc;
    return {};
  }

  /** @brief ディスパッチテーブル用: filter_pad（引数1: 幅, 引数2: 埋め文字literal index） */
  static std::expected<void, error_ctx> handle_string_filter_arg_pad(bc_executor& ex, std::size_t& pc, std::string& filtered) {
    auto arg     = static_cast<int>(ex.bc_.instructions[pc].operand);
    auto pad_str = (ex.bc_.instructions[pc].operand2 != UINT32_MAX) ? std::string_view{ex.bc_.literals[ex.bc_.instructions[pc].operand2]} : std::string_view{};
    apply_string_filter(filtered, {.filter = string_filter::pad, .arg1 = arg, .str_arg1 = pad_str});
    ++pc;
    return {};
  }

  /** @brief ディスパッチテーブル用: filter_pluralize（引数1: 単数literal index, 引数2: 複数literal index） */
  static std::expected<void, error_ctx> handle_string_filter_arg_pluralize(bc_executor& ex, std::size_t& pc, std::string& filtered) {
    auto singular = ex.bc_.literals[ex.bc_.instructions[pc].operand];
    auto plural   = ex.bc_.literals[ex.bc_.instructions[pc].operand2];
    apply_string_filter(filtered, {.filter = string_filter::pluralize, .str_arg1 = singular, .str_arg2 = plural});
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

  /** @brief JSONシリアライズヘルパー（glz::write_json を呼ぶ） */
  template <class Buffer, class V>
  static void json_serialize_value(Buffer& out, V const& value) {
    if constexpr (glz::reflectable<V>) {
      auto ec = glz::write_json(value, out);
      (void)ec;
    } else if constexpr (serializable_v<V>) {
      serialize_value(out, value);
    }
  }

  static std::expected<void, error_ctx> handle_resolve_filtered(bc_executor& ex, std::size_t& pc, std::string& filtered) {
    auto const& instr  = ex.bc_.instructions[pc];
    auto const& var_ref = ex.bc_.var_refs[instr.operand2];
    filtered.clear();
    // コンパイル時事前計算済みフラグで判定（ループ不要）
    bool use_json = (var_ref.filter_flags & 1) != 0;
    bool use_chrono_format = (var_ref.filter_flags & 2) != 0;
    std::string_view chrono_fmt;
    if (use_chrono_format) {
      for (auto const& f : var_ref.filters) {
        if (f.filter == string_filter::format) { chrono_fmt = f.str_arg1; break; }
      }
    }
    auto r = for_each_field(ex.value_, var_ref.key, var_ref.field_index, var_ref.has_dot, [&](auto const& field) {
      using FT = std::remove_cvref_t<decltype(field)>;
      if (use_json) {
        json_serialize_value(filtered, field);
      } else if constexpr (is_chrono_time_point_v<FT>) {
        if (use_chrono_format) {
          serialize_chrono(filtered, field, chrono_fmt);
        } else {
          serialize_chrono(filtered, field);
        }
      } else if constexpr (std::is_arithmetic_v<FT> && !std::same_as<FT, bool>) {
        if (use_chrono_format) {
          serialize_formatted(filtered, field, chrono_fmt);
        } else {
          serialize_value(filtered, field);
        }
      } else if constexpr (std::same_as<FT, std::string> || std::same_as<FT, std::string_view>) {
        if (use_chrono_format) {
          serialize_formatted(filtered, field, chrono_fmt);
        } else {
          serialize_value(filtered, field);
        }
      } else if constexpr (is_std_optional_v<FT>) {
        if (field.has_value()) {
          using inner_t = std::remove_cvref_t<decltype(*field)>;
          if constexpr (is_chrono_time_point_v<inner_t>) {
            if (use_chrono_format) {
              serialize_chrono(filtered, *field, chrono_fmt);
            } else {
              serialize_chrono(filtered, *field);
            }
          } else if constexpr (std::is_arithmetic_v<inner_t> && !std::same_as<inner_t, bool>) {
            if (use_chrono_format) {
              serialize_formatted(filtered, *field, chrono_fmt);
            } else {
              serialize_value(filtered, *field);
            }
          } else if constexpr (std::same_as<inner_t, std::string> || std::same_as<inner_t, std::string_view>) {
            if (use_chrono_format) {
              serialize_formatted(filtered, *field, chrono_fmt);
            } else {
              serialize_value(filtered, *field);
            }
          } else {
            serialize_value(filtered, *field);
          }
        }
      } else if constexpr (serializable_v<FT>) {
        serialize_value(filtered, field);
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
    auto        else_pc  = instr.operand3;
    bool        is_falsy = true;
    bool        entered = false;
    if (body_end <= pc + 1 || body_end > ex.bc_.instructions.size())
      return std::unexpected(error_ctx{.position = pc, .ec = error_code::syntax_error});
    auto section_body = [&](auto const& field) -> std::expected<void, error_ctx> {
      entered = true;
      using FT = std::remove_cvref_t<decltype(field)>;
      if constexpr (ct_is_vector_like<FT>) {
        if (!field.empty()) is_falsy = false;
        using elem_t = typename FT::value_type;
        bc_loop_state ls;
        ls.parent = ex.loop_;
        ls.count = static_cast<std::uint32_t>(field.size());
        for (ls.index = 0; ls.index < static_cast<std::uint32_t>(field.size()); ++ls.index) {
          ls.continue_flag = false;
          ls.binding_name = ref.key;
          ls.binding_elem = &field[ls.index];
          ls.binding_resolve = &resolve_binding_var<elem_t>;
          ls.binding_truthy = &eval_binding_truthy<elem_t>;
          bc_executor<elem_t, RootT> child_exec(ex.bc_, field[ls.index], ex.root_value_, &ls, ex.out_);
          auto r2 = child_exec.execute_impl(pc + 1, body_end - 1);
          if (!r2) return r2;
          if (ls.continue_flag) { ls.continue_flag = false; continue; }
          if (ls.break_flag) break;
        }
      } else if constexpr (std::same_as<FT, bool>) {
        is_falsy = !field;
        if (field) { auto r2 = ex.execute_impl(pc + 1, body_end - 1); if (!r2) return r2; }
      } else if constexpr (is_std_optional_v<FT>) {
        is_falsy = !field.has_value();
        if (field.has_value()) {
          using inner_t = typename FT::value_type;
          bc_executor<inner_t, RootT> child_exec(ex.bc_, *field, ex.root_value_, nullptr, ex.out_);
          auto r2 = child_exec.execute_impl(pc + 1, body_end - 1);
          if (!r2) return r2;
        }
      } else if constexpr (ct_is_map_like<FT>) {
        if (!field.empty()) is_falsy = false;
        bc_loop_state ls;
        ls.parent = ex.loop_;
        ls.count = static_cast<std::uint32_t>(field.size());
        for (auto const& [k, v] : field) {
          ls.key = std::string_view{k};
          using val_t = std::remove_cvref_t<decltype(v)>;
          ls.binding_name = ref.key;
          ls.binding_elem = &v;
          ls.binding_resolve = &resolve_binding_var<val_t>;
          ls.binding_truthy = &eval_binding_truthy<val_t>;
          bc_executor<val_t, RootT> child_exec(ex.bc_, v, ex.root_value_, &ls, ex.out_);
          auto r2 = child_exec.execute_impl(pc + 1, body_end - 1);
          if (!r2) return r2;
          if (ls.break_flag) break;
          ++ls.index;
        }
      } else if constexpr (ct_is_set_like<FT>) {
        if (!field.empty()) is_falsy = false;
        using elem_t = typename FT::value_type;
        bc_loop_state ls;
        ls.parent = ex.loop_;
        ls.count = static_cast<std::uint32_t>(field.size());
        for (auto const& elem : field) {
          ls.continue_flag = false;
          ls.binding_name = ref.key;
          ls.binding_elem = &elem;
          ls.binding_resolve = &resolve_binding_var<elem_t>;
          ls.binding_truthy = &eval_binding_truthy<elem_t>;
          bc_executor<elem_t, RootT> child_exec(ex.bc_, elem, ex.root_value_, &ls, ex.out_);
          auto r2 = child_exec.execute_impl(pc + 1, body_end - 1);
          if (!r2) return r2;
          if (ls.continue_flag) { ls.continue_flag = false; continue; }
          if (ls.break_flag) break;
          ++ls.index;
        }
      } else if constexpr (std::same_as<FT, std::string> || std::same_as<FT, std::string_view>) {
        if (!field.empty()) {
          is_falsy = false;
          bc_loop_state guard;
          guard.parent = ex.loop_;
          bc_loop_state const* save = ex.loop_;
          const_cast<bc_loop_state const*&>(ex.loop_) = &guard;
          auto r2 = ex.execute_impl(pc + 1, body_end - 1);
          const_cast<bc_loop_state const*&>(ex.loop_) = save;
          if (!r2) return r2;
        }
      } else if constexpr (ct_glz_reflectable<FT>) {
        is_falsy = false;
        constexpr auto sz = glz::reflect<FT>::size;
        auto tied = glz::to_tie(field);
        std::expected<void, error_ctx> res{};
        [&]<std::size_t... I>(std::index_sequence<I...>) {
          (([&] {
             if (!res) return;
             using elem_t = std::remove_cvref_t<decltype(glz::get<I>(tied))>;
             bc_loop_state ls;
             ls.parent = ex.loop_;
             ls.count = sz; ls.index = I; ls.key = glz::reflect<FT>::keys[I];
             ls.binding_name = ref.key;
             ls.binding_elem = &glz::get<I>(tied);
             ls.binding_resolve = &resolve_binding_var<elem_t>;
             ls.binding_truthy = &eval_binding_truthy<elem_t>;
             bc_executor<elem_t, RootT> child_exec(ex.bc_, glz::get<I>(tied), ex.root_value_, &ls, ex.out_);
             res = child_exec.execute_impl(pc + 1, body_end - 1);
          }()), ...);
        }(std::make_index_sequence<sz>{});
        return res;
      }
      return {};
    };
    auto r = for_each_field(ex.value_, ref.key, ref.field_index, ref.has_dot, section_body);
    if (!r) return std::unexpected(r.error());
    if (!entered) {
      auto r2 = for_each_field(ex.root_value_, ref.key, ref.field_index, ref.has_dot, section_body);
      if (!r2) return std::unexpected(r2.error());
    }
    if (else_pc > 0 && is_falsy) {
      pc = else_pc;
    } else {
      pc = body_end;
    }
    return {};
  }

  static std::expected<void, error_ctx> handle_emit_inverted(bc_executor& ex, std::size_t& pc, std::string&) {
    auto const& instr = ex.bc_.instructions[pc];
    auto const& ref   = ex.bc_.var_refs[instr.operand2];
    auto        else_pc = instr.operand3;
    bool empty = true;
    (void)for_each_field(ex.value_, ref.key, ref.field_index, ref.has_dot, [&](auto const& field) {
      using FT = std::remove_cvref_t<decltype(field)>;
      if constexpr (ct_is_vector_like<FT>) { empty = field.empty(); }
      else if constexpr (std::same_as<FT, bool>) { empty = !field; }
      else if constexpr (is_std_optional_v<FT>) { empty = !field.has_value(); }
      else if constexpr (ct_is_map_like<FT>) { empty = field.empty(); }
      else if constexpr (ct_is_set_like<FT>) { empty = field.empty(); }
      else if constexpr (std::same_as<FT, std::string> || std::same_as<FT, std::string_view>) { empty = field.empty(); }
      else if constexpr (std::is_arithmetic_v<FT>) { empty = (field == 0); }
      else if constexpr (std::is_enum_v<FT>) { empty = (static_cast<std::underlying_type_t<FT>>(field) == 0); }
      else if constexpr (ct_glz_reflectable<FT>) { empty = false; }
    });
    auto body_end = instr.operand;
    if (body_end <= pc + 1 || body_end > ex.bc_.instructions.size())
      return std::unexpected(error_ctx{.position = pc, .ec = error_code::syntax_error});
    if (empty) {
      auto r = ex.execute_impl(pc + 1, body_end - 1);
      if (!r) return std::unexpected(r.error());
      pc = body_end;
    } else if (else_pc > 0) {
      auto r = ex.execute_impl(else_pc, body_end - 1);
      if (!r) return std::unexpected(r.error());
      pc = body_end;
    } else {
      pc = body_end;
    }
    return {};
  }

  static std::expected<void, error_ctx> handle_emit_if(bc_executor& ex, std::size_t& pc, std::string&) {
    auto const& instr = ex.bc_.instructions[pc];
    auto const& ref   = ex.bc_.var_refs[instr.operand2];
    bool cond = false;
    if (ref.key.starts_with("loop.parent.")) {
      eval_loop_parent_truthy(ex, ref.key, cond);
    } else if (ref.key.starts_with("loop.")) {
      if (ex.loop_) {
        if (ref.key == "loop.is_last") { cond = (ex.loop_->index + 1 == ex.loop_->count); }
        else if (ref.key == "loop.is_first") { cond = (ex.loop_->index == 0); }
        else if (ref.key == "loop.index") { cond = (ex.loop_->index != 0); }
        else if (ref.key == "loop.key") { cond = !ex.loop_->key.empty(); }
      }
    } else {
      bool found = false;
      (void)for_each_field(ex.value_, ref.key, ref.field_index, ref.has_dot, [&](auto const& field) {
        using FT = std::remove_cvref_t<decltype(field)>;
        if constexpr (std::same_as<FT, bool>) { cond = field; }
        else if constexpr (ct_is_vector_like<FT>) { cond = !field.empty(); }
        else if constexpr (std::same_as<FT, std::string> || std::same_as<FT, std::string_view>) { cond = !field.empty(); }
        else if constexpr (std::is_arithmetic_v<FT>) { cond = (field != 0); }
        else if constexpr (std::is_enum_v<FT>) { cond = (static_cast<std::underlying_type_t<FT>>(field) != 0); }
        else if constexpr (is_std_optional_v<FT>) { cond = field.has_value(); }
        else if constexpr (ct_is_map_like<FT>) { cond = !field.empty(); }
        else if constexpr (ct_is_set_like<FT>) { cond = !field.empty(); }
        found = true;
      });
      if (!found) {
        for (auto* lp = ex.loop_; lp; lp = lp->parent) {
          if (lp->binding_truthy && (ref.key == lp->binding_name ||
              (ref.key.starts_with(lp->binding_name) && ref.key[lp->binding_name.size()] == '.'))) {
            std::string_view sub = (ref.key == lp->binding_name)
                                       ? std::string_view{}
                                       : std::string_view{ref.key.data() + lp->binding_name.size() + 1,
                                                          ref.key.size() - lp->binding_name.size() - 1};
            cond = lp->binding_truthy(lp->binding_elem, sub);
            break;
          }
        }
      }
    }
    if (!cond) { pc = instr.operand; } else { ++pc; }
    return {};
  }

  static std::expected<void, error_ctx> handle_emit_if_cmp(bc_executor& ex, std::size_t& pc, std::string&) {
    auto const& instr = ex.bc_.instructions[pc];
    auto const& ref   = ex.bc_.var_refs[instr.operand2];
    int rhs = ref.int_filters.empty() ? 0 : ref.int_filters[0].arg;
    bool cond = false;
    (void)for_each_field(ex.value_, ref.key, ref.field_index, ref.has_dot, [&](auto const& field) {
      using FT = std::remove_cvref_t<decltype(field)>;
      if constexpr (std::is_arithmetic_v<FT>) {
        auto lv = static_cast<long long>(field);
        auto rv = static_cast<long long>(rhs);
        switch (instr.op) {
        case bc_opcode::emit_if_eq:  cond = (lv == rv); break;
        case bc_opcode::emit_if_ne:  cond = (lv != rv); break;
        case bc_opcode::emit_if_gt:  cond = (lv > rv);  break;
        case bc_opcode::emit_if_gte: cond = (lv >= rv); break;
        case bc_opcode::emit_if_lt:  cond = (lv < rv);  break;
        case bc_opcode::emit_if_lte: cond = (lv <= rv); break;
        default: break;
        }
      } else if constexpr (std::is_enum_v<FT>) {
        /** enum LHS: underlying 整数に変換して算術比較と同じロジックで評価 */
        auto lv = static_cast<long long>(static_cast<std::underlying_type_t<FT>>(field));
        auto rv = static_cast<long long>(rhs);
        switch (instr.op) {
        case bc_opcode::emit_if_eq:  cond = (lv == rv); break;
        case bc_opcode::emit_if_ne:  cond = (lv != rv); break;
        case bc_opcode::emit_if_gt:  cond = (lv > rv);  break;
        case bc_opcode::emit_if_gte: cond = (lv >= rv); break;
        case bc_opcode::emit_if_lt:  cond = (lv < rv);  break;
        case bc_opcode::emit_if_lte: cond = (lv <= rv); break;
        default: break;
        }
      } else if constexpr (std::same_as<FT, std::string> || std::same_as<FT, std::string_view>) {
        if (ref.compare_rhs_kind == compare_operand_kind::string_literal) {
          switch (instr.op) {
          case bc_opcode::emit_if_eq: cond = (field == ref.compare_rhs_text); break;
          case bc_opcode::emit_if_ne: cond = (field != ref.compare_rhs_text); break;
          default: break;
          }
        }
      }
    });
    if (!cond) { pc = instr.operand; } else { ++pc; }
    return {};
  }

  static std::expected<void, error_ctx> handle_emit_if_logic(bc_executor& ex, std::size_t& pc, std::string&) {
    auto const& instr = ex.bc_.instructions[pc];
    auto const& lhs_ref = ex.bc_.var_refs[instr.operand2];
    bool cond = false;
    auto eval_truthy = [&](std::string_view key, std::uint32_t field_idx, bool has_dot) -> bool {
      bool result = false;
      bool found = false;
      if (!eval_loop_parent_truthy(ex, key, result)) {
        (void)for_each_field(ex.value_, key, field_idx, has_dot, [&](auto const& field) {
          using FT = std::remove_cvref_t<decltype(field)>;
          if constexpr (std::same_as<FT, bool>) { result = field; }
          else if constexpr (ct_is_vector_like<FT>) { result = !field.empty(); }
          else if constexpr (std::same_as<FT, std::string> || std::same_as<FT, std::string_view>) { result = !field.empty(); }
          else if constexpr (std::is_arithmetic_v<FT>) { result = (field != 0); }
          else if constexpr (std::is_enum_v<FT>) { result = (static_cast<std::underlying_type_t<FT>>(field) != 0); }
          else if constexpr (is_std_optional_v<FT>) { result = field.has_value(); }
          else if constexpr (ct_is_map_like<FT>) { result = !field.empty(); }
          else if constexpr (ct_is_set_like<FT>) { result = !field.empty(); }
          found = true;
        });
      }
      if (found) return result;
      for (auto* lp = ex.loop_; lp; lp = lp->parent) {
        if (lp->binding_truthy && (key == lp->binding_name ||
            (key.starts_with(lp->binding_name) && key[lp->binding_name.size()] == '.'))) {
          std::string_view sub = (key == lp->binding_name)
                                     ? std::string_view{}
                                     : std::string_view{key.data() + lp->binding_name.size() + 1,
                                                        key.size() - lp->binding_name.size() - 1};
          return lp->binding_truthy(lp->binding_elem, sub);
        }
      }
      return false;
    };
    bool lhs = eval_truthy(lhs_ref.key, lhs_ref.field_index, lhs_ref.has_dot);
    if (instr.op == bc_opcode::emit_if_not) {
      cond = !lhs;
    } else {
      auto const& rhs_ref = ex.bc_.var_refs[instr.operand3];
      bool rhs_val = eval_truthy(rhs_ref.key, rhs_ref.field_index, rhs_ref.has_dot);
      cond = (instr.op == bc_opcode::emit_if_or) ? (lhs || rhs_val) : (lhs && rhs_val);
    }
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

  static std::expected<void, error_ctx> handle_call_partial(bc_executor& ex, std::size_t& pc, std::string&) {
    auto const& instr = ex.bc_.instructions[pc];
    auto const& entry = ex.bc_.partial_entries[instr.operand];
    bc_executor<T, RootT> child_exec(*entry.bc, ex.value_, ex.root_value_, ex.loop_, ex.out_);
    auto r = child_exec.execute();
    if (!r)
      return std::unexpected(r.error());
    ++pc;
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

    // Fast path: simple emit_litvar + emit_literal + halt only
    // Skips computed-goto dispatch overhead for common trivial templates.
    // is_simple はコンパイル時に決定済み（実行時走査なし）。
    if constexpr (INJAMM_FAST_PATH) {
      if (bc_.is_simple) {
          for (auto i = start; i < end; ++i) {
            auto const& instr = bc_.instructions[i];
            switch (instr.op) {
              case bc_opcode::emit_literal:
                out_.append(bc_.literals[instr.operand]);
                break;
              case bc_opcode::emit_litvar:
              case bc_opcode::emit_litvar_raw: {
                out_.append(bc_.literals[instr.operand]);
                auto const& ref = bc_.var_refs[instr.operand2];
                bool raw = (instr.op == bc_opcode::emit_litvar_raw);
                if (!ref.is_loop_parent || !resolve_loop_parent_var(*this, ref.key, raw)) {
                  auto r = for_each_field(value_, ref.key, ref.field_index, ref.has_dot,
                    [&](auto const& field) { emit_var_value(field, raw); });
                  if (!r) return r;
                }
                break;
              }
              default:
                return {};
            }
          }
          return {};
        }
      }

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
       &&L_emit_if_cmp,             // 11 emit_if_eq
       &&L_emit_if_cmp,             // 12 emit_if_ne
        &&L_emit_if_cmp,             // 13 emit_if_gt (falls through to L_emit_if_eq handler)
        &&L_emit_if_cmp,             // 14 emit_if_gte
        &&L_emit_if_cmp,             // 15 emit_if_lt
        &&L_emit_if_cmp,             // 16 emit_if_lte
        &&L_emit_else,               // 17
        &&L_emit_endif,              // 18
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
        &&L_filter_replace,          // 40
        &&L_filter_default,          // 41
        &&L_filter_json,             // 42
        &&L_filter_safe,             // 43
        &&L_filter_indent,           // 44
        &&L_filter_pad,              // 45
        &&L_filter_pluralize,        // 46
        &&L_filter_format,           // 47
        &&L_emit_filtered,           // 48
        &&L_emit_filtered_raw,       // 49
        &&L_filter_int_abs,          // 50
        &&L_filter_int_hex,          // 51
        &&L_filter_int_oct,          // 52
        &&L_filter_int_bin,          // 53
        &&L_filter_int_neg,          // 54
        &&L_filter_int_mod,          // 55
        &&L_filter_int_numify,       // 56
        &&L_filter_int_is_neg,       // 57
        &&L_filter_int_eq,           // 58
        &&L_filter_int_ne,           // 59
        &&L_filter_int_gt,           // 60
        &&L_filter_int_gte,          // 61
        &&L_filter_int_lt,           // 62
        &&L_filter_int_lte,          // 63
        &&L_filter_int_zerofill,     // 64
        &&L_filter_int_add,          // 65
        &&L_filter_int_sub,          // 66
        &&L_filter_int_mul,          // 67
        &&L_filter_int_div,          // 68
        &&L_filter_float_precision,  // 69
        &&L_emit_if_filtered,        // 70
        &&L_emit_break,              // 71
        &&L_emit_continue,           // 72
        &&L_emit_at_index1,          // 73
        &&L_emit_at_size,            // 74
        &&L_emit_var_size,           // 75
        &&L_emit_if_logic,           // 76 emit_if_or
        &&L_emit_if_logic,           // 77 emit_if_and
        &&L_emit_if_not,             // 78 emit_if_not
        &&L_call_partial,            // 79
        &&L_halt,                    // 80
    };

/** @brief 現在の命令のオペコードに対応するラベルにジャンプする（実行範囲外なら終了） */
#define DISPATCH()                                                         \
  do {                                                                     \
    if (pc >= end)                                                         \
      goto L_halt;                                                         \
    goto* dispatch_table[static_cast<int>(bc_.instructions[pc].op)];       \
  } while (0)

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
    if (ref.is_loop_parent && resolve_loop_parent_var(*this, ref.key, raw)) { ++pc; DISPATCH(); }
    bool        found = false;
    auto        r = for_each_field(value_, ref.key, ref.field_index, ref.has_dot, [&](auto const& field) { found = true; emit_var_value(field, raw); });
    if (!r) return r;
    if (found) { ++pc; DISPATCH(); }
    if (try_resolve_loop_binding(*this, ref, raw)) { ++pc; DISPATCH(); }
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
    bool entered = false;
    auto section_iterate = [&](auto const& field) -> std::expected<void, error_ctx> {
      entered = true;
      using FT = std::remove_cvref_t<decltype(field)>;
      if constexpr (ct_is_vector_like<FT>) {
        if (!field.empty()) is_falsy = false;
        using elem_t = typename FT::value_type;
        bc_loop_state ls;
        ls.parent = loop_;
        ls.count = static_cast<std::uint32_t>(field.size());

        for (ls.index = 0; ls.index < static_cast<std::uint32_t>(field.size()); ++ls.index) {
          ls.continue_flag = false;
          ls.binding_name = ref.key;
          ls.binding_elem = &field[ls.index];
          ls.binding_resolve = &resolve_binding_var<elem_t>;
          ls.binding_truthy = &eval_binding_truthy<elem_t>;
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
        ls.parent = loop_;
        ls.count = static_cast<std::uint32_t>(field.size());
        for (auto const& [k, v] : field) {
          ls.key      = std::string_view{k};
          using val_t = std::remove_cvref_t<decltype(v)>;
          ls.binding_name = ref.key;
          ls.binding_elem = &v;
          ls.binding_resolve = &resolve_binding_var<val_t>;
          ls.binding_truthy = &eval_binding_truthy<val_t>;
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
        ls.parent = loop_;
        ls.count = static_cast<std::uint32_t>(field.size());
        for (auto const& elem : field) {
          ls.continue_flag = false;
          ls.binding_name = ref.key;
          ls.binding_elem = &elem;
          ls.binding_resolve = &resolve_binding_var<elem_t>;
          ls.binding_truthy = &eval_binding_truthy<elem_t>;
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
      } else if constexpr (std::same_as<FT, std::string> || std::same_as<FT, std::string_view>) {
        if (!field.empty()) {
          is_falsy = false;
          bc_loop_state guard;
          guard.parent = loop_;
          bc_loop_state const* save = loop_;
          loop_ = &guard;
          auto r2 = execute_impl(pc + 1, body_end - 1);
          loop_ = save;
          if (!r2) return r2;
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
             using elem_t = std::remove_cvref_t<decltype(glz::get<I>(tied))>;
             bc_loop_state ls;
             ls.parent = loop_;
             ls.count     = sz;
             ls.index     = I;
             ls.key       = glz::reflect<FT>::keys[I];
             ls.binding_name = ref.key;
             ls.binding_elem = &glz::get<I>(tied);
             ls.binding_resolve = &resolve_binding_var<elem_t>;
             ls.binding_truthy = &eval_binding_truthy<elem_t>;
             bc_executor<elem_t, RootT> child_exec(bc_, glz::get<I>(tied), root_value_, &ls, out_);
             res = child_exec.execute_impl(pc + 1, body_end - 1);
           }()),
           ...);
        }(std::make_index_sequence<sz>{});
        return res;
      }
      return {};
    };

    auto r = for_each_field(value_, ref.key, ref.field_index, ref.has_dot, section_iterate);
    if (!r)
      return r;
    if (!entered) {
      auto r2 = for_each_field(root_value_, ref.key, ref.field_index, ref.has_dot, section_iterate);
      if (!r2)
        return r2;
    }

    if (else_pc > 0 && is_falsy) {
      pc = else_pc;
    } else {
      pc = body_end;
    }
    DISPATCH();
  }

  /** @brief 実行終端（通常到達しない） */
  L_emit_end: { ++pc; DISPATCH(); }

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
      } else if constexpr (std::is_enum_v<FT>) {
        /** enum: underlying 整数が 0 なら偽（空）扱い */
        empty = (static_cast<std::underlying_type_t<FT>>(field) == 0);
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
        out_.append(buf.data(), static_cast<std::size_t>(ptr - buf.data()));
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
        out_.append(buf.data(), static_cast<std::size_t>(ptr - buf.data()));
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
        out_.append(buf.data(), static_cast<std::size_t>(ptr - buf.data()));
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
        out_.append(buf.data(), static_cast<std::size_t>(ptr - buf.data()));
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

    /** loop.parent.* 変数の解決 */
    if (ref.key.starts_with("loop.parent.")) {
      eval_loop_parent_truthy(*this, ref.key, cond);
    } else if (ref.key.starts_with("loop.")) {
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
      bool found = false;
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
        } else if constexpr (std::is_enum_v<FT>) {
          /** enum: underlying 整数が 0 でなければ真 */
          cond = (static_cast<std::underlying_type_t<FT>>(field) != 0);
        } else if constexpr (is_std_optional_v<FT>) {
          cond = field.has_value();
        } else if constexpr (ct_is_map_like<FT>) {
          cond = !field.empty();
        } else if constexpr (ct_is_set_like<FT>) {
          cond = !field.empty();
        }
        found = true;
      });
      if (!found) {
        for (auto* lp = loop_; lp; lp = lp->parent) {
          if (lp->binding_truthy && (ref.key == lp->binding_name ||
              (ref.key.starts_with(lp->binding_name) && ref.key[lp->binding_name.size()] == '.'))) {
            std::string_view sub = (ref.key == lp->binding_name)
                                       ? std::string_view{}
                                       : std::string_view{ref.key.data() + lp->binding_name.size() + 1,
                                                          ref.key.size() - lp->binding_name.size() - 1};
            cond = lp->binding_truthy(lp->binding_elem, sub);
            break;
          }
        }
      }
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
  L_emit_if_cmp: {
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
        auto lv = static_cast<long long>(field);
        auto rv = static_cast<long long>(rhs);
        switch (instr.op) {
        case bc_opcode::emit_if_eq:  cond = (lv == rv); break;
        case bc_opcode::emit_if_ne:  cond = (lv != rv); break;
        case bc_opcode::emit_if_gt:  cond = (lv > rv);  break;
        case bc_opcode::emit_if_gte: cond = (lv >= rv); break;
        case bc_opcode::emit_if_lt:  cond = (lv < rv);  break;
        case bc_opcode::emit_if_lte: cond = (lv <= rv); break;
        default: break;
        }
        } else if constexpr (std::is_enum_v<FT>) {
          /** enum LHS: underlying 整数に変換して算術比較と同じロジックで評価 */
          auto lv = static_cast<long long>(static_cast<std::underlying_type_t<FT>>(field));
          auto rv = static_cast<long long>(rhs);
          switch (instr.op) {
          case bc_opcode::emit_if_eq:  cond = (lv == rv); break;
          case bc_opcode::emit_if_ne:  cond = (lv != rv); break;
          case bc_opcode::emit_if_gt:  cond = (lv > rv);  break;
          case bc_opcode::emit_if_gte: cond = (lv >= rv); break;
          case bc_opcode::emit_if_lt:  cond = (lv < rv);  break;
          case bc_opcode::emit_if_lte: cond = (lv <= rv); break;
          default: break;
          }
        } else if constexpr (std::same_as<FT, std::string> || std::same_as<FT, std::string_view>) {
          if (ref.compare_rhs_kind == compare_operand_kind::string_literal) {
            switch (instr.op) {
            case bc_opcode::emit_if_eq: cond = (field == ref.compare_rhs_text); break;
            case bc_opcode::emit_if_ne: cond = (field != ref.compare_rhs_text); break;
            default: break;
            }
          }
        }
      });
    if (!cond) {
      pc = instr.operand;
    } else {
      ++pc;
    }
    DISPATCH();
  }

  /** @brief if (!a): 単項否定 */
  L_emit_if_not: {
    auto const& instr = bc_.instructions[pc];
    auto const& ref   = bc_.var_refs[instr.operand2];
    bool result = false;
    bool found = false;
    (void)for_each_field(value_, ref.key, ref.field_index, ref.has_dot, [&](auto const& field) {
      using FT = std::remove_cvref_t<decltype(field)>;
      if constexpr (std::same_as<FT, bool>) { result = field; }
      else if constexpr (ct_is_vector_like<FT>) { result = !field.empty(); }
      else if constexpr (std::same_as<FT, std::string> || std::same_as<FT, std::string_view>) { result = !field.empty(); }
      else if constexpr (std::is_arithmetic_v<FT>) { result = (field != 0); }
      else if constexpr (std::is_enum_v<FT>) { result = (static_cast<std::underlying_type_t<FT>>(field) != 0); }
      else if constexpr (is_std_optional_v<FT>) { result = field.has_value(); }
      else if constexpr (ct_is_map_like<FT>) { result = !field.empty(); }
      else if constexpr (ct_is_set_like<FT>) { result = !field.empty(); }
      found = true;
    });
    if (!found) {
      for (auto* lp = loop_; lp; lp = lp->parent) {
        if (lp->binding_truthy && (ref.key == lp->binding_name ||
            (ref.key.starts_with(lp->binding_name) && ref.key[lp->binding_name.size()] == '.'))) {
          std::string_view sub = (ref.key == lp->binding_name) ? std::string_view{} : ref.key.substr(lp->binding_name.size() + 1);
          result = lp->binding_truthy(lp->binding_elem, sub);
          break;
        }
      }
    }
    if (!result) {
      ++pc;
    } else {
      pc = instr.operand;
    }
    DISPATCH();
  }

  /** @brief if (a || b) / if (a && b): 二項論理演算 */
  L_emit_if_logic: {
    auto const& instr  = bc_.instructions[pc];
    auto const& lhs_ref = bc_.var_refs[instr.operand2];
    auto eval_truthy = [&](bc_var_ref const& ref) -> bool {
      bool result = false;
      bool found = false;
      (void)for_each_field(value_, ref.key, ref.field_index, ref.has_dot, [&](auto const& field) {
        using FT = std::remove_cvref_t<decltype(field)>;
        if constexpr (std::same_as<FT, bool>) { result = field; }
        else if constexpr (ct_is_vector_like<FT>) { result = !field.empty(); }
        else if constexpr (std::same_as<FT, std::string> || std::same_as<FT, std::string_view>) { result = !field.empty(); }
        else if constexpr (std::is_arithmetic_v<FT>) { result = (field != 0); }
        else if constexpr (std::is_enum_v<FT>) { result = (static_cast<std::underlying_type_t<FT>>(field) != 0); }
        else if constexpr (is_std_optional_v<FT>) { result = field.has_value(); }
        else if constexpr (ct_is_map_like<FT>) { result = !field.empty(); }
        else if constexpr (ct_is_set_like<FT>) { result = !field.empty(); }
        found = true;
      });
      if (found) return result;
      for (auto* lp = loop_; lp; lp = lp->parent) {
        if (lp->binding_truthy && (ref.key == lp->binding_name ||
            (ref.key.starts_with(lp->binding_name) && ref.key[lp->binding_name.size()] == '.'))) {
          std::string_view sub = (ref.key == lp->binding_name) ? std::string_view{} : ref.key.substr(lp->binding_name.size() + 1);
          return lp->binding_truthy(lp->binding_elem, sub);
        }
      }
      return false;
    };
    bool lhs = eval_truthy(lhs_ref);
    auto const& rhs_ref = bc_.var_refs[instr.operand3];
    bool rhs = eval_truthy(rhs_ref);
    bool cond = (instr.op == bc_opcode::emit_if_or) ? (lhs || rhs) : (lhs && rhs);
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
    if (ref.is_loop_parent && resolve_loop_parent_var(*this, ref.key, raw)) { ++pc; DISPATCH(); }
    bool        found = false;
    auto        r = for_each_field(value_, ref.key, ref.field_index, ref.has_dot, [&](auto const& field) { found = true; emit_var_value(field, raw); });
    if (!r) return r;
    if (found) { ++pc; DISPATCH(); }
    if (try_resolve_loop_binding(*this, ref, raw)) { ++pc; DISPATCH(); }
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
          out_.append(buf.data(), static_cast<std::size_t>(ptr - buf.data()));
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
    // コンパイル時事前計算済みフラグで判定（ループ不要）
    bool use_json = (var_ref.filter_flags & 1) != 0;
    bool use_chrono_format = (var_ref.filter_flags & 2) != 0;
    std::string_view chrono_fmt;
    if (use_chrono_format) {
      for (auto const& f : var_ref.filters) {
        if (f.filter == string_filter::format) { chrono_fmt = f.str_arg1; break; }
      }
    }
    auto r = for_each_field(value_, var_ref.key, var_ref.field_index, var_ref.has_dot, [&](auto const& field) {
      using FT = std::remove_cvref_t<decltype(field)>;
      if (use_json) {
        json_serialize_value(filtered_value_, field);
      } else if constexpr (is_chrono_time_point_v<FT>) {
        if (use_chrono_format) {
          serialize_chrono(filtered_value_, field, chrono_fmt);
        } else {
          serialize_chrono(filtered_value_, field);
        }
      } else if constexpr (std::is_arithmetic_v<FT> && !std::same_as<FT, bool>) {
        if (use_chrono_format) {
          serialize_formatted(filtered_value_, field, chrono_fmt);
        } else {
          serialize_value(filtered_value_, field);
        }
      } else if constexpr (std::same_as<FT, std::string> || std::same_as<FT, std::string_view>) {
        if (use_chrono_format) {
          serialize_formatted(filtered_value_, field, chrono_fmt);
        } else {
          serialize_value(filtered_value_, field);
        }
      } else if constexpr (is_std_optional_v<FT>) {
        if (field.has_value()) {
          using inner_t = std::remove_cvref_t<decltype(*field)>;
          if constexpr (is_chrono_time_point_v<inner_t>) {
            if (use_chrono_format) {
              serialize_chrono(filtered_value_, *field, chrono_fmt);
            } else {
              serialize_chrono(filtered_value_, *field);
            }
          } else if constexpr (std::is_arithmetic_v<inner_t> && !std::same_as<inner_t, bool>) {
            if (use_chrono_format) {
              serialize_formatted(filtered_value_, *field, chrono_fmt);
            } else {
              serialize_value(filtered_value_, *field);
            }
          } else if constexpr (std::same_as<inner_t, std::string> || std::same_as<inner_t, std::string_view>) {
            if (use_chrono_format) {
              serialize_formatted(filtered_value_, *field, chrono_fmt);
            } else {
              serialize_value(filtered_value_, *field);
            }
          } else {
            serialize_value(filtered_value_, *field);
          }
        }
      } else if constexpr (serializable_v<FT>) {
        serialize_value(filtered_value_, field);
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

  /** @brief デフォルト値フィルタ */
  L_filter_default: {
    auto const& instr = bc_.instructions[pc];
    apply_string_filter(filtered_value_, {.filter = string_filter::default_value, .str_arg1 = bc_.literals[instr.operand]});
    ++pc;
    DISPATCH();
  }

  /** @brief JSON出力フィルタ（L_resolve_filtered で処理済み） */
  L_filter_json: {
    ++pc;
    DISPATCH();
  }

  /** @brief 生出力マーク（コンパイル時処理済み） */
  L_filter_safe: {
    ++pc;
    DISPATCH();
  }

  /** @brief 行インデント */
  L_filter_indent: {
    apply_string_filter(filtered_value_, {.filter = string_filter::indent, .arg1 = static_cast<int>(bc_.instructions[pc].operand)});
    ++pc;
    DISPATCH();
  }

  /** @brief パディング */
  L_filter_pad: {
    auto arg     = static_cast<int>(bc_.instructions[pc].operand);
    auto pad_str = (bc_.instructions[pc].operand2 != UINT32_MAX) ? std::string_view{bc_.literals[bc_.instructions[pc].operand2]} : std::string_view{};
    apply_string_filter(filtered_value_, {.filter = string_filter::pad, .arg1 = arg, .str_arg1 = pad_str});
    ++pc;
    DISPATCH();
  }

  /** @brief 単数形/複数形 */
  L_filter_pluralize: {
    apply_string_filter(filtered_value_, {.filter = string_filter::pluralize, .str_arg1 = bc_.literals[bc_.instructions[pc].operand], .str_arg2 = bc_.literals[bc_.instructions[pc].operand2]});
    ++pc;
    DISPATCH();
  }

  /** @brief strftime 形式 chrono フォーマット（L_resolve_filtered で処理済み、no-op） */
  L_filter_format: {
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

  /** @brief partial呼び出し: プリコンパイル済みpartialバイトコードをサブexecutorで実行 */
  L_call_partial: {
    auto const& instr    = bc_.instructions[pc];
    auto const& entry    = bc_.partial_entries[instr.operand];
    {
      bc_executor<T, RootT> child_exec(*entry.bc, value_, root_value_, loop_, out_);
      auto r = child_exec.execute();
      if (!r)
        return r;
    }
    ++pc;
    DISPATCH();
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
      &handle_emit_if_cmp,        // 12 emit_if_gt
      &handle_emit_if_cmp,        // 13 emit_if_gte
      &handle_emit_if_cmp,        // 14 emit_if_lt
      &handle_emit_if_cmp,        // 15 emit_if_lte
      &handle_emit_else,          // 16
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
      &handle_string_filter,      // 37 filter_default (generic handler with literal arg)
      &handle_noop,               // 38 filter_json
      &handle_noop,               // 39 filter_safe
      &handle_string_filter_arg,  // 40 filter_indent
      &handle_string_filter_arg_pad, // 41 filter_pad
      &handle_string_filter_arg_pluralize, // 42 filter_pluralize
      &handle_noop,                        // 43 filter_format (no-op)
      &handle_emit_filtered,               // 44
      &handle_emit_filtered,               // 45 emit_filtered_raw
      &handle_int_filter,                  // 46 filter_int_abs
      &handle_int_filter,                  // 47 filter_int_hex
      &handle_int_filter,                  // 48 filter_int_oct
      &handle_int_filter,                  // 49 filter_int_bin
      &handle_int_filter,                  // 50 filter_int_neg
      &handle_int_filter,                  // 51 filter_int_mod
      &handle_int_filter,                  // 52 filter_int_numify
      &handle_int_filter,                  // 53 filter_int_is_neg
      &handle_int_filter,                  // 54 filter_int_eq
      &handle_int_filter,                  // 55 filter_int_ne
      &handle_int_filter,                  // 56 filter_int_gt
      &handle_int_filter,                  // 57 filter_int_gte
      &handle_int_filter,                  // 58 filter_int_lt
      &handle_int_filter,                  // 59 filter_int_lte
      &handle_int_filter,                  // 60 filter_int_zerofill
      &handle_int_filter,                  // 61 filter_int_add
      &handle_int_filter,                  // 62 filter_int_sub
      &handle_int_filter,                  // 63 filter_int_mul
      &handle_int_filter,                  // 64 filter_int_div
      &handle_float_filter,                // 65 filter_float_precision
      &handle_emit_if_filtered,            // 66
      &handle_emit_break,                  // 67
      &handle_emit_continue,               // 68
      &handle_emit_at_index1,              // 69
      &handle_emit_at_size,                // 70
      &handle_emit_var_size,               // 71
      &handle_emit_if_logic,               // 72 emit_if_or
      &handle_emit_if_logic,               // 73 emit_if_and
      &handle_emit_if_logic,               // 74 emit_if_not
      &handle_call_partial,                // 75
      &handle_emit_halt,                   // 76
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

  /**
   * @brief 全変数参照の値サイズ合計を推定する
   */
  template <class U>
  static std::size_t estimate_var_sizes(bytecode const& bc, U const& value) {
    std::size_t total = 0;
    for (auto const& ref : bc.var_refs) {
      auto r = for_each_field(value, ref.key, ref.field_index, ref.has_dot, [&](auto const& field) -> std::expected<void, error_ctx> {
        total += estimate_field_size(field);
        return {};
      });
      if (!r) total += 32;
    }
    return total;
  }

private:
  template <class V>
  static std::size_t estimate_field_size(V const& v) {
    using FT = std::remove_cvref_t<V>;
    if constexpr (std::same_as<FT, bool>) {
      return 5;
    } else if constexpr (is_std_optional_v<FT>) {
      return v.has_value() ? estimate_field_size(*v) : 0;
    } else if constexpr (std::same_as<FT, std::string> || std::same_as<FT, std::string_view>) {
      return v.size();
    } else if constexpr (std::is_enum_v<FT>) {
#ifndef INJAMM_NO_ENUM_REGISTRY
      auto name = enchantum::to_string(v);
      if (!name.empty()) return name.size();
#endif
      return 10;
    } else if constexpr (std::is_arithmetic_v<FT>) {
      return 16;
    }
    return 16;
  }
};

/**
 * @brief 出力バッファのサイズ見積もりを計算する
 * @tparam T コンテキスト値の型
 * @param bc バイトコード
 * @param value コンテキスト値
 * @return 推定出力サイズ
 */
template <class T>
std::size_t estimate_output_size(bytecode const& bc, T const& value) {
  auto base = bc.literal_total_size * 4;
  if (bc.var_refs.size() > 5) {
    return base + bc_executor<T>::template estimate_var_sizes<>(bc, value);
  }
  return base + bc.var_refs.size() * 32;
}

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
  auto        estimated = estimate_output_size(bc, value);
  if (estimated < 256) estimated = 256;
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
  auto estimated = estimate_output_size(bc, value);
  if (estimated < 256) estimated = 256;
  if (out.capacity() < estimated)
    out.reserve(estimated);
  bc_executor<T> exec(bc, value, value, nullptr, out);
  return exec.execute();
}

}  // namespace injamm::detail
