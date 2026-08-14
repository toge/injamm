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

// handle_* は computed goto ラベルから呼ばれる。execute_impl が巨大なため GCC の
// サイズベースインライナがスキップし、呼び出しコストがホットパスに乗る（計測で
// section_loop が ~20% 劣化）。always_inline で強制インライン化し、直書きラベルと
// 同等のコードにする。MSVC 等未対応コンパイラでは空マクロに退化する。
#if defined(__GNUC__) || defined(__clang__)
#define INJAMM_ALWAYS_INLINE [[gnu::always_inline]]
#else
#define INJAMM_ALWAYS_INLINE
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
#include <span>
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
  bool (*binding_resolve)(std::string&, std::string_view, bool, void const*, std::string_view, std::uint32_t) = nullptr;
  /**< 現在要素を key に従って出力する型消去リゾルバ（末尾はサブパスの field_index ヒント） */
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
template <class T, class RootT = T, class Sink = std::string>
  requires output_sink<Sink>
class bc_executor {
  bytecode const&      bc_;
  T const&             value_;
  RootT const&         root_value_;
  bc_loop_state const* loop_ = nullptr;
  Sink&                out_;
  std::string          emit_this_scratch_;

  /**
   * @brief フィールドインデックス指定で visitor を適用する共通ヘルパ
   * @details fold 式でインデックスをコンパイル時定数化して直接アクセスする。
   *          呼び出し側で index の妥当性（範囲・名前一致）を検証済みであること。
   */
  template <class V, class F>
  static auto visit_field_by_index(V const& v, std::uint32_t field_index, F&& visitor) -> std::expected<void, error_ctx> {
    constexpr auto sz   = static_cast<std::size_t>(glz::reflect<V>::size);
    auto           tied = glz::to_tie(v);
    using visitor_t     = decltype(visitor(glz::get<0>(tied)));
    std::expected<void, error_ctx> result{};
    [&]<std::size_t... I>(std::index_sequence<I...>) {
      auto try_index = [&]<std::size_t Idx>() -> bool {
        if (field_index == Idx) {
          if constexpr (std::same_as<visitor_t, void>) {
            visitor(glz::get<Idx>(tied));
          } else {
            result = visitor(glz::get<Idx>(tied));
          }
          return true;
        }
        return false;
      };
      (void)(try_index.template operator()<I>() || ...);
    }(std::make_index_sequence<sz>{});
    return result;
  }

  /**
   * @brief 末端ノードの処理：パス全体をキーとしてフィールドを探索しvisitorを適用
   * @param hint コンパイル時解決済みフィールドインデックス（UINT32_MAX で線形探索）
   */
  template <class V, class F>
  static auto process_terminal_node(V const& v, std::string_view path, F&& visitor, std::uint32_t hint = UINT32_MAX) -> std::expected<void, error_ctx> {
    if constexpr (ct_glz_reflectable<V>) {
      constexpr auto sz = static_cast<std::size_t>(glz::reflect<V>::size);
      /** ヒントが有効なら名前検証のみで O(1) アクセス */
      if (hint < sz && std::string_view{glz::reflect<V>::keys[hint]} == path) {
        return visit_field_by_index(v, hint, std::forward<F>(visitor));
      }
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
   * @param hints 階層別フィールドインデックスヒント / depth 現在の階層
   */
  template <class V, class F>
  static auto process_intermediate_node(V const& v, std::string_view first_key, std::string_view rest_path, F&& visitor,
                                        std::span<std::uint32_t const> hints = {}, std::size_t depth = 0) -> std::expected<void, error_ctx> {
    if constexpr (ct_glz_reflectable<V>) {
      constexpr auto sz   = static_cast<std::size_t>(glz::reflect<V>::size);
      /** 発見したフィールドに対する再帰処理（O(1) パスと線形パスで共用） */
      auto descend = [&](auto const& field) -> std::expected<void, error_ctx> {
        using FT = std::remove_cvref_t<decltype(field)>;
        if constexpr (ct_glz_reflectable<FT>) {
          return resolve_nested_path(field, rest_path, std::forward<F>(visitor), hints, depth + 1);
        } else if constexpr (ct_is_vector_like<FT>) {
          auto idx_dot = rest_path.find('.');
          auto idx_str = rest_path.substr(0, idx_dot);
          return process_vector_index_access(field, rest_path, idx_dot, idx_str, std::forward<F>(visitor));
        } else {
          return {};
        }
      };
      /** ヒントが有効なら名前検証のみで O(1) アクセス */
      auto const hint = depth < hints.size() ? hints[depth] : UINT32_MAX;
      if (hint < sz && std::string_view{glz::reflect<V>::keys[hint]} == first_key) {
        return visit_field_by_index(v, hint, descend);
      }
      auto           tied = glz::to_tie(v);
      using visitor_r     = decltype(visitor(glz::get<0>(tied)));

      if constexpr (std::same_as<visitor_r, void>) {
        /** visitor が void の場合: フィールドを発見次第再帰 */
        [&]<std::size_t... I>(std::index_sequence<I...>) {
          (([&] {
             if (std::string_view{glz::reflect<V>::keys[I]} == first_key) {
               (void)descend(glz::get<I>(tied));
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
               result = descend(glz::get<I>(tied));
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
   * @param hints コンパイル時解決済みの階層別フィールドインデックス（名前検証付きで使用）
   * @param depth 現在の階層（hints の添字）
   * @return std::expected<void, error_ctx> エラー発生時に unexpected を返す
   * @details パスを最初のキーと残りのパスに分割し、最初のキーでフィールドを検索してから
   *          残りのパスで再帰する。Glaze のコンパイル時リフレクションにより
   *          フィールド名の比較を展開する。
   */
template <class V, class F>
static auto resolve_nested_path(V const& v, std::string_view path, F&& visitor,
                                std::span<std::uint32_t const> hints = {}, std::size_t depth = 0) -> std::expected<void, error_ctx> {
  /** ドットの位置で分割: 末端に達したか判定 */
  auto dot_pos = path.find('.');
  if (dot_pos == std::string_view::npos) {
    /** 末端: パス全体をキーとして一致するフィールドを探し visitor を適用 */
    return process_terminal_node(v, path, std::forward<F>(visitor), depth < hints.size() ? hints[depth] : UINT32_MAX);
  }

  /** 中間ノード: 最初のキーを取得し、残りのパスで再帰 */
  auto first_key = path.substr(0, dot_pos);
  auto rest_path = path.substr(dot_pos + 1);
  return process_intermediate_node(v, first_key, rest_path, std::forward<F>(visitor), hints, depth);
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
static auto for_each_field(V const& v, std::string_view key, std::uint32_t field_index, bool has_dot, F&& visitor,
                           std::span<std::uint32_t const> path_hints = {}) -> std::expected<void, error_ctx> {
  if constexpr (forward_iterable<V>) {
    if (key == ".") {
      if constexpr (std::same_as<decltype(visitor(v)), void>) {
        visitor(v);
        return {};
      } else {
        return visitor(v);
      }
    }
    return {};
  }

  /** ネストパスが含まれている場合は再帰解決に委譲（階層別インデックスヒント付き） */
  if (has_dot) {
    return resolve_nested_path(v, key, std::forward<F>(visitor), path_hints);
  }

  if constexpr (runtime_field_accessible<V>) {
    auto val = v.find(key);
    if constexpr (std::same_as<decltype(visitor(val)), void>) {
      visitor(val);
      return {};
    } else {
      return visitor(val);
    }
  } else if constexpr (ct_glz_reflectable<V>) {
    constexpr auto sz   = static_cast<std::size_t>(glz::reflect<V>::size);
    auto           tied = glz::to_tie(v);
    using visitor_t     = decltype(visitor(glz::get<0>(tied)));

    /**
     * O(1) アクセス: field_index が有効な場合
     * if constexpr チェーンで実行時の field_index 値をコンパイル時定数に変換し、
     * 該当フィールドに直接アクセスする。コンパイラが二分探索的ジャンプテーブルを生成。
     * フィールド数が多い構造で線形探索より高速（計測で sz>=5 相当が分岐点）。
     */
    if constexpr (sz >= 5) {
      if (field_index != UINT32_MAX && field_index < sz && std::string_view{glz::reflect<V>::keys[field_index]} == key) {
        auto visit_by_index = [&]<std::size_t... I>(std::index_sequence<I...>) -> std::expected<void, error_ctx> {
          std::expected<void, error_ctx> visitor_result{};
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
          bool found = (try_index.template operator()<I>() || ...);
          (void)found;
          return visitor_result;
        };
        return visit_by_index(std::make_index_sequence<sz>{});
      }
    }

    /**
     * フォールバック: フィールド数の少ない構造は線形探索が速い。
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

  /** @brief bc_var_ref からヒント込みで for_each_field を呼ぶ簡易ラッパ
   *  @details 非ドット参照（大多数）では path_indices のロードを行わず、
   *           従来と同一のコードパスを維持する。 */
  template <class V, class F>
  static auto for_each_field_ref(V const& v, bc_var_ref const& ref, F&& visitor) -> std::expected<void, error_ctx> {
    return for_each_field(v, ref.key, ref.field_index, ref.has_dot, std::forward<F>(visitor),
                          std::span<std::uint32_t const>{ref.path_indices.data(), ref.path_hint_len});
  }

  /**
   * @brief フィールドの値を出力バッファに追記する
   * @param field 出力対象のフィールド
   * @param raw HTMLエスケープを行わない場合は true
   * @details 型に応じて適切なシリアライズを行う:
   *          - bool → "true" / "false"
   *          - std::string / std::string_view → raw フラグに応じてエスケープ有無
   *          - 算術型 → std::to_chars による高速変換
   *          - enum → serialize_enum (enchantum)
   *          - カスタムstruct → serializable_v<FT> なら serialize_value、ct_glz_reflectable なら glz::write_json
   */
  /** @brief フィールド値を指定バッファに追記する（束縛リゾルバ兼用の静的版） */
  template <class Buffer>
  static void emit_value_static(Buffer& out, auto const& field, bool raw) {
    using FT = std::remove_cvref_t<decltype(field)>;
    if constexpr (std::same_as<FT, bool>) {
      if (field) {
        out.append("true", 4);
      } else {
        out.append("false", 5);
      }
    } else if constexpr (std::same_as<FT, std::string> || std::same_as<FT, std::string_view> || char_pointer_v<FT>) {
      auto sv = to_sv(field);
      if (raw) {
        out.append(sv.data(), sv.size());
      } else {
        html_escape_into(out, sv);
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
    } else if constexpr (serializable_v<FT>) {
      std::string scratch;
      serialize_value(scratch, field);
      if (raw) {
        out.append(scratch);
      } else {
        html_escape_into(out, scratch);
      }
    } else if constexpr (ct_glz_reflectable<FT> && glz::write_supported<FT, glz::JSON>) {
      std::string scratch;
      (void)glz::write_json(field, scratch);
      if (raw) {
        out.append(scratch);
      } else {
        html_escape_into(out, scratch);
      }
    }
  }

  void emit_var_value(auto const& field, bool raw) { emit_value_static(out_, field, raw); }

  /** @brief 数値を出力バッファに追記する共通ヘルパ */
  template <class Buffer>
  static void append_number(Buffer& out, std::uint32_t v) {
    std::array<char, 16> buf;
    auto [ptr, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), v);
    if (ec == std::errc{}) out.append(buf.data(), static_cast<std::size_t>(ptr - buf.data()));
  }

  /** @brief loop.parent.* 変数の解決。解決できれば true を返す（kind はコンパイル時分類済み） */
  static auto resolve_loop_parent_var(bc_executor const& ex, special_var_kind kind, bool raw) -> bool {
    // 呼び出し側で ref.is_loop_parent により事前ゲート済み。文字列比較は不要。
    if (!ex.loop_) return false;
    auto parent = ex.loop_->parent;
    if (!parent) return false;
    switch (kind) {
    case special_var_kind::lp_index:
      append_number(ex.out_, parent->index);
      return true;
    case special_var_kind::lp_index1:
      append_number(ex.out_, parent->index + 1);
      return true;
    case special_var_kind::lp_size:
      append_number(ex.out_, parent->count);
      return true;
    case special_var_kind::lp_is_first:
      ex.out_.append(parent->index == 0 ? "true" : "false");
      return true;
    case special_var_kind::lp_is_last:
      ex.out_.append((parent->index + 1 == parent->count) ? "true" : "false");
      return true;
    case special_var_kind::lp_key:
      if (!parent->key.empty()) {
        if (raw) { ex.out_.append(parent->key); } else { html_escape_into(ex.out_, parent->key); }
      }
      return true;
    default:
      return false;
    }
  }

  /**
   * @brief 変数参照の truthiness を評価する共通実装
   * @details special kind のコンパイル時分類により this / loop.* / loop.parent.* を
   *          整数比較のみで判定する。通常キーはフィールド走査＋ループ束縛フォールバック。
   */
  static bool eval_var_truthy(bc_executor const& ex, bc_var_ref const& ref) {
    auto* lp0 = ex.loop_;
    switch (ref.special) {
    case special_var_kind::this_:
      return (lp0 && lp0->binding_truthy) ? lp0->binding_truthy(lp0->binding_elem, std::string_view{}) : false;
    case special_var_kind::loop_index:    return lp0 && lp0->index != 0;
    case special_var_kind::loop_index1:   return lp0 != nullptr;
    case special_var_kind::loop_size:     return lp0 && lp0->count != 0;
    case special_var_kind::loop_is_first: return lp0 && lp0->index == 0;
    case special_var_kind::loop_is_last:  return lp0 && lp0->index + 1 == lp0->count;
    case special_var_kind::loop_key:      return lp0 && !lp0->key.empty();
    case special_var_kind::loop_unknown:  return false;
    case special_var_kind::lp_index:      return lp0 && lp0->parent && lp0->parent->index != 0;
    case special_var_kind::lp_index1:     return lp0 && lp0->parent != nullptr;
    case special_var_kind::lp_size:       return lp0 && lp0->parent && lp0->parent->count != 0;
    case special_var_kind::lp_is_first:   return lp0 && lp0->parent && lp0->parent->index == 0;
    case special_var_kind::lp_is_last:    return lp0 && lp0->parent && lp0->parent->index + 1 == lp0->parent->count;
    case special_var_kind::lp_key:        return lp0 && lp0->parent && !lp0->parent->key.empty();
    case special_var_kind::lp_unknown:    return false;
    case special_var_kind::none:
    default:
      break;
    }
    bool result = false;
    bool found  = false;
    (void)for_each_field_ref(ex.value_, ref,[&](auto const& field) {
      using FT = std::remove_cvref_t<decltype(field)>;
      if constexpr (std::same_as<FT, bool>) { result = field; }
      else if constexpr (ct_is_vector_like<FT>) { result = !field.empty(); }
      else if constexpr (std::same_as<FT, std::string> || std::same_as<FT, std::string_view> || char_pointer_v<FT>) { result = !to_sv(field).empty(); }
      else if constexpr (std::is_arithmetic_v<FT>) { result = (field != 0); }
      else if constexpr (std::is_enum_v<FT>) { result = (static_cast<std::underlying_type_t<FT>>(field) != 0); }
      else if constexpr (is_std_optional_v<FT>) { result = field.has_value(); }
      else if constexpr (ct_is_map_like<FT>) { result = !field.empty(); }
      else if constexpr (ct_is_set_like<FT>) { result = !field.empty(); }
      found = true;
    });
    if (found) return result;
    for (auto* lp = ex.loop_; lp; lp = lp->parent) {
      if (lp->binding_truthy && (ref.key == lp->binding_name ||
          (ref.key.starts_with(lp->binding_name) && ref.key[lp->binding_name.size()] == '.'))) {
        std::string_view sub = (ref.key == lp->binding_name)
                                   ? std::string_view{}
                                   : std::string_view{ref.key.data() + lp->binding_name.size() + 1,
                                                      ref.key.size() - lp->binding_name.size() - 1};
        return lp->binding_truthy(lp->binding_elem, sub);
      }
    }
    return false;
  }

  /** @brief ループ内束縛の型消去リゾルバ: 現在要素を key に従って出力する */
  template <class ElemT>
  static bool resolve_binding_var(std::string& out, std::string_view key, bool raw, void const* elem, std::string_view binding_name, std::uint32_t sub_field_index) {
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
      (void)for_each_field(e, sub, sub_field_index, sub.find('.') != std::string_view::npos,
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
          else if constexpr (std::same_as<FT, std::string> || std::same_as<FT, std::string_view> || char_pointer_v<FT>) res = !to_sv(f).empty();
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
      else if constexpr (std::same_as<ElemT, std::string> || std::same_as<ElemT, std::string_view> || char_pointer_v<ElemT>) return !to_sv(e).empty();
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
        if constexpr (std::is_same_v<Sink, std::string>) {
          // std::string 出力は直接書き込み（scratch 経由だと余分な確保+コピーが発生する）
          return lp->binding_resolve(ex.out_, ref.key, raw, lp->binding_elem, lp->binding_name, ref.field_index);
        } else {
          // 型消去リゾルバは std::string& にしか書けないため scratch 経由で sink に流す
          std::string scratch;
          bool ok = lp->binding_resolve(scratch, ref.key, raw, lp->binding_elem, lp->binding_name, ref.field_index);
          if (ok) ex.out_.append(scratch);
          return ok;
        }
      }
    }
    return false;
  }

  // -- shared section/inverted/filter implementations (both dispatch paths delegate here) --

  /** @brief セクションフィルタパイプラインからイテレーションウィンドウを畳み込む
   *  @details ops を順に適用し、[lo, hi) + 方向 (bwd) を計算する。 */
  struct section_window {
    std::uint32_t lo = 0;
    std::uint32_t hi = 0;
    bool bwd = false;
  };

  static section_window fold_section_ops(bc_var_ref const& ref, std::uint32_t sz) {
    section_window w{0, sz, false};
    for (std::uint8_t i = 0; i < ref.section_op_count; ++i) {
      auto const& op = ref.section_ops[i];
      auto n = static_cast<std::uint32_t>(op.arg);
      switch (op.kind) {
        case section_filter_op_kind::reverse:
          w.bwd = !w.bwd;
          break;
        case section_filter_op_kind::take:
          if (w.bwd) w.lo = (w.hi > n ? std::max(w.lo, w.hi - n) : w.lo);
          else       w.hi = std::min(w.hi, w.lo + n);
          break;
        case section_filter_op_kind::skip:
          if (w.bwd) w.hi = (w.hi > n ? w.hi - n : w.lo);
          else       w.lo = std::min(w.hi, w.lo + n);
          break;
        case section_filter_op_kind::take_last:
          if (w.bwd) w.hi = std::min(w.hi, w.lo + n);
          else       w.lo = (w.hi > n ? std::max(w.lo, w.hi - n) : w.lo);
          break;
        case section_filter_op_kind::skip_last:
          if (w.bwd) w.lo = std::min(w.hi, w.lo + n);
          else       w.hi = (w.hi > n ? w.hi - n : w.lo);
          break;
      }
    }
    return w;
  }

  static std::expected<void, error_ctx> do_section(bc_executor& ex, std::size_t& pc) {
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
        auto const sz = static_cast<std::uint32_t>(field.size());
        auto w = fold_section_ops(ref, sz);
        auto count = w.hi - w.lo;
        using elem_t = typename FT::value_type;
        bc_loop_state ls;
        ls.parent = ex.loop_;
        ls.count = count;
        for (ls.index = 0; ls.index < count; ++ls.index) {
          ls.continue_flag = false;
          auto idx = w.bwd ? (w.hi - 1u - ls.index) : (w.lo + ls.index);
          auto const& elem = field[idx];
          ls.binding_name = ref.key;
          ls.binding_elem = &elem;
          ls.binding_resolve = &resolve_binding_var<elem_t>;
          ls.binding_truthy = &eval_binding_truthy<elem_t>;
          bc_executor<elem_t, RootT, Sink> child_exec(ex.bc_, elem, ex.root_value_, &ls, ex.out_);
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
          bc_executor<inner_t, RootT, Sink> child_exec(ex.bc_, *field, ex.root_value_, nullptr, ex.out_);
          auto r2 = child_exec.execute_impl(pc + 1, body_end - 1);
          if (!r2) return r2;
        }
      } else if constexpr (ct_is_map_like<FT>) {
        if (!field.empty()) is_falsy = false;
        auto const sz = static_cast<std::uint32_t>(field.size());
        auto w = fold_section_ops(ref, sz);
        auto count = w.hi - w.lo;
        bc_loop_state ls;
        ls.parent = ex.loop_;
        ls.count = count;
        std::uint32_t emitted = 0;
        auto visit = [&](auto const& k, auto const& v) -> std::expected<void, error_ctx> {
          if (emitted >= count) return {};
          ls.key = std::string_view{k};
          using val_t = std::remove_cvref_t<decltype(v)>;
          ls.binding_name = ref.key;
          ls.binding_elem = &v;
          ls.binding_resolve = &resolve_binding_var<val_t>;
          ls.binding_truthy = &eval_binding_truthy<val_t>;
          bc_executor<val_t, RootT, Sink> child_exec(ex.bc_, v, ex.root_value_, &ls, ex.out_);
          auto r2 = child_exec.execute_impl(pc + 1, body_end - 1);
          if (r2) { ++emitted; ++ls.index; }
          return r2;
        };
        std::expected<void, error_ctx> map_res{};
        if (w.bwd) {
          using pair_t = std::remove_cvref_t<decltype(*field.begin())>;
          std::vector<pair_t> temp(field.begin(), field.end());
          for (std::uint32_t i = 0; i < count && !map_res && !ls.break_flag; ++i) {
            auto idx = w.hi - 1u - i;
            map_res = visit(temp[idx].first, temp[idx].second);
          }
        } else {
          std::uint32_t skipped = 0;
          for (auto const& [k, v] : field) {
            if (skipped < w.lo) { ++skipped; continue; }
            map_res = visit(k, v);
            if (!map_res || ls.break_flag) break;
          }
        }
        if (!map_res) return map_res;
      } else if constexpr (ct_is_set_like<FT>) {
        if (!field.empty()) is_falsy = false;
        using elem_t = typename FT::value_type;
        auto const sz = static_cast<std::uint32_t>(field.size());
        auto w = fold_section_ops(ref, sz);
        auto count = w.hi - w.lo;
        bc_loop_state ls;
        ls.parent = ex.loop_;
        ls.count = count;
        std::uint32_t emitted = 0;
        if (w.bwd) {
          std::vector<elem_t> temp(field.begin(), field.end());
          for (std::uint32_t i = 0; i < count; ++i) {
            auto idx = w.hi - 1u - i;
            auto const& elem = temp[idx];
            ls.index = emitted;
            ls.continue_flag = false;
            ls.binding_name = ref.key;
            ls.binding_elem = &elem;
            ls.binding_resolve = &resolve_binding_var<elem_t>;
            ls.binding_truthy = &eval_binding_truthy<elem_t>;
            bc_executor<elem_t, RootT, Sink> child_exec(ex.bc_, elem, ex.root_value_, &ls, ex.out_);
            auto r2 = child_exec.execute_impl(pc + 1, body_end - 1);
            if (!r2) return r2;
            if (ls.continue_flag) { ls.continue_flag = false; --emitted; continue; }
            if (ls.break_flag) break;
            ++emitted;
          }
        } else {
          std::uint32_t skipped = 0;
          for (auto const& elem : field) {
            if (skipped < w.lo) { ++skipped; continue; }
            if (emitted >= count) break;
            ls.index = emitted;
            ls.continue_flag = false;
            ls.binding_name = ref.key;
            ls.binding_elem = &elem;
            ls.binding_resolve = &resolve_binding_var<elem_t>;
            ls.binding_truthy = &eval_binding_truthy<elem_t>;
            bc_executor<elem_t, RootT, Sink> child_exec(ex.bc_, elem, ex.root_value_, &ls, ex.out_);
            auto r2 = child_exec.execute_impl(pc + 1, body_end - 1);
            if (!r2) return r2;
            if (ls.continue_flag) { ls.continue_flag = false; continue; }
            if (ls.break_flag) break;
            ++emitted;
          }
        }
      } else if constexpr (std::same_as<FT, std::string> || std::same_as<FT, std::string_view> || char_pointer_v<FT>) {
        if (!to_sv(field).empty()) {
          is_falsy = false;
          bc_loop_state guard;
          guard.parent = ex.loop_;
          bc_loop_state const* save = ex.loop_;
          ex.loop_ = &guard;
          auto r2 = ex.execute_impl(pc + 1, body_end - 1);
          ex.loop_ = save;
          if (!r2) return r2;
        }
      } else if constexpr (forward_iterable<FT>) {
        using elem_t = typename FT::value_type;
        bc_loop_state ls;
        ls.parent = ex.loop_;
        ls.count = 0;
        ls.index = 0;
        auto it = field.begin();
        auto end_field = field.end();
        if (!(it != end_field)) return {};
        is_falsy = false;
        // std::distance が使えない型（end() が nullptr_t を返す等）でも安全に処理するため、
        // ウィンドウ計算は size() が利用可能な場合のみ行い、それ以外はストリーミング。
        constexpr bool has_size = requires { field.size(); };
        if constexpr (has_size) {
          auto const sz = static_cast<std::uint32_t>(field.size());
          auto w = fold_section_ops(ref, sz);
          auto count = w.hi - w.lo;
          ls.count = count;
          if (w.bwd) {
            if constexpr (std::bidirectional_iterator<decltype(it)>) {
              auto rit = field.rbegin();
              auto rend = field.rend();
              std::uint32_t emitted = 0;
              for (; rit != rend; ++rit) {
                if (emitted >= count) break;
                auto const& elem = *rit;
                ls.index = emitted;
                ls.continue_flag = false;
                ls.binding_name = ref.key;
                ls.binding_elem = &elem;
                ls.binding_resolve = &resolve_binding_var<elem_t>;
                ls.binding_truthy = &eval_binding_truthy<elem_t>;
                bc_executor<elem_t, RootT, Sink> child_exec(ex.bc_, elem, ex.root_value_, &ls, ex.out_);
                auto r2 = child_exec.execute_impl(pc + 1, body_end - 1);
                if (!r2) return r2;
                if (ls.continue_flag) { ls.continue_flag = false; continue; }
                if (ls.break_flag) break;
                ++emitted;
              }
              ls.count = emitted;
            } else {
              // forward-only: 逆順不可、forward+skip のみ
              std::uint32_t emitted = 0;
              std::uint32_t skipped = 0;
              for (; it != end_field; ++it) {
                if (skipped < w.lo) { ++skipped; continue; }
                if (emitted >= count) break;
                auto const& elem = *it;
                ls.index = emitted;
                ls.continue_flag = false;
                ls.binding_name = ref.key;
                ls.binding_elem = &elem;
                ls.binding_resolve = &resolve_binding_var<elem_t>;
                ls.binding_truthy = &eval_binding_truthy<elem_t>;
                bc_executor<elem_t, RootT, Sink> child_exec(ex.bc_, elem, ex.root_value_, &ls, ex.out_);
                auto r2 = child_exec.execute_impl(pc + 1, body_end - 1);
                if (!r2) return r2;
                if (ls.continue_flag) { ls.continue_flag = false; continue; }
                if (ls.break_flag) break;
                ++emitted;
              }
              ls.count = emitted;
            }
          } else {
            std::uint32_t skipped = 0;
            std::uint32_t emitted = 0;
            for (; it != end_field; ++it, ++ls.index) {
              if (skipped < w.lo) { ++skipped; continue; }
              if (emitted >= count) break;
              auto const& elem = *it;
              ls.continue_flag = false;
              ls.binding_name = ref.key;
              ls.binding_elem = &elem;
              ls.binding_resolve = &resolve_binding_var<elem_t>;
              ls.binding_truthy = &eval_binding_truthy<elem_t>;
              bc_executor<elem_t, RootT, Sink> child_exec(ex.bc_, elem, ex.root_value_, &ls, ex.out_);
              auto r2 = child_exec.execute_impl(pc + 1, body_end - 1);
              if (!r2) return r2;
              if (ls.continue_flag) { ls.continue_flag = false; continue; }
              if (ls.break_flag) break;
              ++emitted;
            }
            ls.count = emitted;
          }
        } else {
          // size() なし: forward/streaming のみ対応、take 制限のみ適用
          std::uint32_t emitted = 0;
          bool const has_ops = ref.section_op_count > 0;
          section_window w{};
          if (has_ops) {
            // size 不明のため、skip/take のストリーミング近似（skip_last/take_last は未対応）
            auto const sz_approx = static_cast<std::uint32_t>(999999u);
            w = fold_section_ops(ref, sz_approx);
          }
          auto count = has_ops ? (w.hi - w.lo) : 999999u;
          ls.count = count;
          std::uint32_t skipped = 0;
          for (; it != end_field; ++it, ++ls.index) {
            if (has_ops && skipped < w.lo) { ++skipped; continue; }
            if (emitted >= count) break;
            auto const& elem = *it;
            ls.continue_flag = false;
            ls.binding_name = ref.key;
            ls.binding_elem = &elem;
            ls.binding_resolve = &resolve_binding_var<elem_t>;
            ls.binding_truthy = &eval_binding_truthy<elem_t>;
            bc_executor<elem_t, RootT, Sink> child_exec(ex.bc_, elem, ex.root_value_, &ls, ex.out_);
            auto r2 = child_exec.execute_impl(pc + 1, body_end - 1);
            if (!r2) return r2;
            if (ls.continue_flag) { ls.continue_flag = false; continue; }
            if (ls.break_flag) break;
            ++emitted;
          }
          ls.count = emitted;
        }
      } else if constexpr (ct_glz_reflectable<FT>) {
        is_falsy = false;
        constexpr auto sz = glz::reflect<FT>::size;
        auto tied = glz::to_tie(field);
        auto w = fold_section_ops(ref, static_cast<std::uint32_t>(sz));
        auto count = w.hi - w.lo;
        std::expected<void, error_ctx> res{};
        if (w.bwd) {
          // リバース: I=0→最後の要素、I=1→その前... のように評価
          [&]<std::size_t... I>(std::index_sequence<I...>) {
            (([&] {
               if (!res) return;
               if (static_cast<std::uint32_t>(I) < w.lo || static_cast<std::uint32_t>(I) >= w.hi) return;
               constexpr auto src_idx = sz - 1u - I;
               using elem_t = std::remove_cvref_t<decltype(glz::get<src_idx>(tied))>;
               bc_loop_state ls;
               ls.parent = ex.loop_;
               ls.count = count; ls.index = static_cast<std::uint32_t>(I - w.lo); ls.key = glz::reflect<FT>::keys[src_idx];
               ls.binding_name = ref.key;
               ls.binding_elem = &glz::get<src_idx>(tied);
               ls.binding_resolve = &resolve_binding_var<elem_t>;
               ls.binding_truthy = &eval_binding_truthy<elem_t>;
               bc_executor<elem_t, RootT, Sink> child_exec(ex.bc_, glz::get<src_idx>(tied), ex.root_value_, &ls, ex.out_);
               res = child_exec.execute_impl(pc + 1, body_end - 1);
            }()), ...);
          }(std::make_index_sequence<sz>{});
        } else {
          // フォワード
          [&]<std::size_t... I>(std::index_sequence<I...>) {
            (([&] {
               if (!res) return;
               if (static_cast<std::uint32_t>(I) < w.lo || static_cast<std::uint32_t>(I) >= w.hi) return;
               constexpr auto src_idx = I;
               using elem_t = std::remove_cvref_t<decltype(glz::get<src_idx>(tied))>;
               bc_loop_state ls;
               ls.parent = ex.loop_;
               ls.count = count; ls.index = static_cast<std::uint32_t>(I - w.lo); ls.key = glz::reflect<FT>::keys[src_idx];
               ls.binding_name = ref.key;
               ls.binding_elem = &glz::get<src_idx>(tied);
               ls.binding_resolve = &resolve_binding_var<elem_t>;
               ls.binding_truthy = &eval_binding_truthy<elem_t>;
               bc_executor<elem_t, RootT, Sink> child_exec(ex.bc_, glz::get<src_idx>(tied), ex.root_value_, &ls, ex.out_);
               res = child_exec.execute_impl(pc + 1, body_end - 1);
            }()), ...);
          }(std::make_index_sequence<sz>{});
        }
        return res;
      }
      return {};
    };
    auto r = for_each_field_ref(ex.value_, ref,section_body);
    if (!r) return std::unexpected(r.error());
    if (!entered) {
      auto r2 = for_each_field_ref(ex.root_value_, ref,section_body);
      if (!r2) return std::unexpected(r2.error());
    }
    if (else_pc > 0 && is_falsy) {
      pc = else_pc;
    } else {
      pc = body_end;
    }
    return {};
  }

  static std::expected<void, error_ctx> do_inverted(bc_executor& ex, std::size_t& pc) {
    auto const& instr = ex.bc_.instructions[pc];
    auto const& ref   = ex.bc_.var_refs[instr.operand2];
    auto        else_pc = instr.operand3;
    bool empty = true;
    (void)for_each_field_ref(ex.value_, ref,[&](auto const& field) {
      using FT = std::remove_cvref_t<decltype(field)>;
      if constexpr (ct_is_vector_like<FT>) { empty = field.empty(); }
      else if constexpr (std::same_as<FT, bool>) { empty = !field; }
      else if constexpr (is_std_optional_v<FT>) { empty = !field.has_value(); }
      else if constexpr (ct_is_map_like<FT>) { empty = field.empty(); }
      else if constexpr (ct_is_set_like<FT>) { empty = field.empty(); }
      else if constexpr (std::same_as<FT, std::string> || std::same_as<FT, std::string_view> || char_pointer_v<FT>) { empty = to_sv(field).empty(); }
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

  static std::expected<void, error_ctx> do_resolve_filtered(bc_executor& ex, std::size_t& pc, std::string& filtered) {
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
    auto r = for_each_field_ref(ex.value_, var_ref,[&](auto const& field) {
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
      } else if constexpr (std::same_as<FT, std::string> || std::same_as<FT, std::string_view> || char_pointer_v<FT>) {
        auto sv = to_sv(field);
        if (use_chrono_format) {
          serialize_formatted(filtered, sv, chrono_fmt);
        } else {
          serialize_value(filtered, sv);
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

  // -- handler functions (shared by both dispatch paths) --

  INJAMM_ALWAYS_INLINE static std::expected<void, error_ctx> handle_emit_literal(bc_executor& ex, std::size_t& pc, std::string&) {
    ex.out_.append(ex.bc_.literals[ex.bc_.instructions[pc].operand]);
    ++pc;
    return {};
  }

  INJAMM_ALWAYS_INLINE static std::expected<void, error_ctx> handle_emit_var(bc_executor& ex, std::size_t& pc, std::string&) {
    bool raw = ex.bc_.instructions[pc].op == bc_opcode::emit_var_raw;
    auto const& ref = ex.bc_.var_refs[ex.bc_.instructions[pc].operand];
    if (ref.is_loop_parent && resolve_loop_parent_var(ex, ref.special, raw)) { ++pc; return {}; }
    if (ref.binding_first && ref.special == special_var_kind::none && try_resolve_loop_binding(ex, ref, raw)) {
      ++pc;
      return {};
    }
    bool        found = false;
    auto        r = for_each_field_ref(ex.value_, ref,[&](auto const& field) { found = true; ex.emit_var_value(field, raw); });
    if (!r && r.error().ec != error_code::unknown_key) return std::unexpected(r.error());
    if (found) { ++pc; return {}; }
    if (try_resolve_loop_binding(ex, ref, raw)) { ++pc; return {}; }
    if (!r) return std::unexpected(r.error());
    ++pc;
    return {};
  }

  INJAMM_ALWAYS_INLINE static std::expected<void, error_ctx> handle_emit_litvar(bc_executor& ex, std::size_t& pc, std::string&) {
    bool raw = ex.bc_.instructions[pc].op == bc_opcode::emit_litvar_raw;
    ex.out_.append(ex.bc_.literals[ex.bc_.instructions[pc].operand]);
    auto const& ref = ex.bc_.var_refs[ex.bc_.instructions[pc].operand2];
    if (ref.is_loop_parent && resolve_loop_parent_var(ex, ref.special, raw)) { ++pc; return {}; }
    if (ref.binding_first && ref.special == special_var_kind::none && try_resolve_loop_binding(ex, ref, raw)) {
      ++pc;
      return {};
    }
    bool        found = false;
    auto        r = for_each_field_ref(ex.value_, ref,[&](auto const& field) { found = true; ex.emit_var_value(field, raw); });
    if (!r && r.error().ec != error_code::unknown_key) return std::unexpected(r.error());
    if (found) { ++pc; return {}; }
    if (try_resolve_loop_binding(ex, ref, raw)) { ++pc; return {}; }
    if (!r) return std::unexpected(r.error());
    ++pc;
    return {};
  }

  INJAMM_ALWAYS_INLINE static std::expected<void, error_ctx> handle_emit_at_root(bc_executor& ex, std::size_t& pc, std::string&) {
    if constexpr (serializable_v<RootT>) {
      serialize_value(ex.out_, ex.root_value_);
    }
    ++pc;
    return {};
  }

  INJAMM_ALWAYS_INLINE static std::expected<void, error_ctx> handle_emit_at_root_field(bc_executor& ex, std::size_t& pc, std::string&) {
    bool raw = ex.bc_.instructions[pc].op == bc_opcode::emit_at_root_field_raw;
    auto const& ref = ex.bc_.var_refs[ex.bc_.instructions[pc].operand];
    auto r = for_each_field_ref(ex.root_value_, ref,[&](auto const& field) { ex.emit_var_value(field, raw); });
    if (!r) return std::unexpected(r.error());
    ++pc;
    return {};
  }

  INJAMM_ALWAYS_INLINE static std::expected<void, error_ctx> handle_emit_this(bc_executor& ex, std::size_t& pc, std::string&) {
    if constexpr (std::same_as<T, std::string> || std::same_as<T, std::string_view> || char_pointer_v<T>) {
      html_escape_into(ex.out_, to_sv(ex.value_));
    } else {
      ex.emit_this_scratch_.clear();
      if constexpr (serializable_v<T>) {
        serialize_value(ex.emit_this_scratch_, ex.value_);
      } else if constexpr (ct_glz_reflectable<T> && glz::write_supported<T, glz::JSON>) {
        if (auto ec = glz::write_json(ex.value_, ex.emit_this_scratch_)) {
          return std::unexpected(error_ctx{.position = pc, .ec = error_code::syntax_error});
        }
      }
      html_escape_into(ex.out_, ex.emit_this_scratch_);
    }
    ++pc;
    return {};
  }

  INJAMM_ALWAYS_INLINE static std::expected<void, error_ctx> handle_emit_var_size(bc_executor& ex, std::size_t& pc, std::string&) {
    auto const& ref = ex.bc_.var_refs[ex.bc_.instructions[pc].operand];
    auto r = for_each_field_ref(ex.value_, ref,[&](auto const& field) {
      using FT = std::remove_cvref_t<decltype(field)>;
      std::size_t sz = 0;
      if constexpr (ct_is_vector_like<FT>) {
        sz = field.size();
      } else if constexpr (ct_is_map_like<FT>) {
        sz = field.size();
      } else if constexpr (ct_is_set_like<FT>) {
        sz = field.size();
      } else if constexpr (std::same_as<FT, std::string> || std::same_as<FT, std::string_view> || char_pointer_v<FT>) {
        sz = to_sv(field).size();
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

  INJAMM_ALWAYS_INLINE static std::expected<void, error_ctx> handle_emit_break(bc_executor& ex, std::size_t& pc, std::string&) {
    if (ex.loop_) ex.loop_->break_flag = true;
    pc = SIZE_MAX;
    return {};
  }

  INJAMM_ALWAYS_INLINE static std::expected<void, error_ctx> handle_emit_continue(bc_executor& ex, std::size_t& pc, std::string&) {
    if (ex.loop_) ex.loop_->continue_flag = true;
    pc = SIZE_MAX;
    return {};
  }

  INJAMM_ALWAYS_INLINE static std::expected<void, error_ctx> handle_emit_at_index(bc_executor& ex, std::size_t& pc, std::string&) {
    if (ex.loop_) {
      std::array<char, 16> buf;
      auto [ptr, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), ex.loop_->index);
      if (ec == std::errc{}) ex.out_.append(buf.data(), static_cast<std::size_t>(ptr - buf.data()));
    }
    ++pc;
    return {};
  }

  INJAMM_ALWAYS_INLINE static std::expected<void, error_ctx> handle_emit_at_index1(bc_executor& ex, std::size_t& pc, std::string&) {
    if (ex.loop_) {
      std::array<char, 16> buf;
      auto [ptr, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), ex.loop_->index + 1);
      if (ec == std::errc{}) ex.out_.append(buf.data(), static_cast<std::size_t>(ptr - buf.data()));
    }
    ++pc;
    return {};
  }

  INJAMM_ALWAYS_INLINE static std::expected<void, error_ctx> handle_emit_at_size(bc_executor& ex, std::size_t& pc, std::string&) {
    if (ex.loop_) {
      std::array<char, 16> buf;
      auto [ptr, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), ex.loop_->count);
      if (ec == std::errc{}) ex.out_.append(buf.data(), static_cast<std::size_t>(ptr - buf.data()));
    }
    ++pc;
    return {};
  }

  INJAMM_ALWAYS_INLINE static std::expected<void, error_ctx> handle_emit_at_first(bc_executor& ex, std::size_t& pc, std::string&) {
    if (ex.loop_ && ex.loop_->index == 0) { ex.out_.append("true"); } else { ex.out_.append("false"); }
    ++pc;
    return {};
  }

  INJAMM_ALWAYS_INLINE static std::expected<void, error_ctx> handle_emit_at_last(bc_executor& ex, std::size_t& pc, std::string&) {
    if (ex.loop_ && ex.loop_->index + 1 == ex.loop_->count) { ex.out_.append("true"); } else { ex.out_.append("false"); }
    ++pc;
    return {};
  }

  INJAMM_ALWAYS_INLINE static std::expected<void, error_ctx> handle_emit_at_key(bc_executor& ex, std::size_t& pc, std::string&) {
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

  /** @brief 汎用文字列フィルタ（operand2 = string_filter 種別） */
  INJAMM_ALWAYS_INLINE static std::expected<void, error_ctx> handle_filter_string(bc_executor& ex, std::size_t& pc, std::string& filtered) {
    auto const& instr = ex.bc_.instructions[pc];
    auto        kind  = static_cast<string_filter>(instr.operand2);
    auto        arg   = static_cast<int>(instr.operand);
    string_filter_entry entry{kind, arg, 0};
    if (kind == string_filter::substr)
      entry.arg2 = static_cast<int>(instr.operand3);
    if (instr.operand3 != UINT32_MAX) {
      auto lit_idx = instr.operand3;
      if (lit_idx < ex.bc_.literals.size())
        entry.str_arg1 = ex.bc_.literals[lit_idx];
      if ((kind == string_filter::replace || kind == string_filter::pluralize) && lit_idx + 1 < ex.bc_.literals.size())
        entry.str_arg2 = ex.bc_.literals[lit_idx + 1];
    }
    apply_string_filter(filtered, entry);
    ++pc;
    return {};
  }

  INJAMM_ALWAYS_INLINE static std::expected<void, error_ctx> handle_filter_int(bc_executor& ex, std::size_t& pc, std::string& filtered) {
    auto const& instr = ex.bc_.instructions[pc];
    auto        kind  = static_cast<int_filter>(instr.operand2);
    if (auto r = apply_int_filter(filtered, {kind, static_cast<int>(instr.operand)}); !r)
      return std::unexpected(r.error());
    ++pc;
    return {};
  }

  INJAMM_ALWAYS_INLINE static std::expected<void, error_ctx> handle_filter_float(bc_executor& ex, std::size_t& pc, std::string& filtered) {
    auto const& instr = ex.bc_.instructions[pc];
    auto        kind  = static_cast<float_filter>(instr.operand2);
    apply_float_filter(filtered, {kind, static_cast<int>(instr.operand)});
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

  INJAMM_ALWAYS_INLINE static std::expected<void, error_ctx> handle_resolve_filtered(bc_executor& ex, std::size_t& pc, std::string& filtered) {
    return do_resolve_filtered(ex, pc, filtered);
  }

  INJAMM_ALWAYS_INLINE static std::expected<void, error_ctx> handle_emit_filtered(bc_executor& ex, std::size_t& pc, std::string& filtered) {
    bool raw = ex.bc_.instructions[pc].op == bc_opcode::emit_filtered_raw;
    if (raw) { ex.out_.append(filtered); } else { html_escape_into(ex.out_, std::string_view{filtered}); }
    ++pc;
    return {};
  }

  INJAMM_ALWAYS_INLINE static std::expected<void, error_ctx> handle_emit_end(bc_executor&, std::size_t& pc, std::string&) {
    ++pc;
    return {};
  }

  INJAMM_ALWAYS_INLINE static std::expected<void, error_ctx> handle_emit_halt(bc_executor&, std::size_t& pc, std::string&) {
    ++pc;
    return {};
  }

  INJAMM_ALWAYS_INLINE static std::expected<void, error_ctx> handle_emit_section(bc_executor& ex, std::size_t& pc, std::string&) {
    return do_section(ex, pc);
  }

  INJAMM_ALWAYS_INLINE static std::expected<void, error_ctx> handle_emit_inverted(bc_executor& ex, std::size_t& pc, std::string&) {
    return do_inverted(ex, pc);
  }

  INJAMM_ALWAYS_INLINE static std::expected<void, error_ctx> handle_emit_if(bc_executor& ex, std::size_t& pc, std::string&) {
    auto const& instr = ex.bc_.instructions[pc];
    auto const& ref   = ex.bc_.var_refs[instr.operand2];
    bool cond = eval_var_truthy(ex, ref);
    if (!cond) { pc = instr.operand; } else { ++pc; }
    return {};
  }

  /** @brief 数値比較の共通ヘルパ（emit_if_eq/ne/gt/gte/lt/lte）。整数・実数共用。 */
  template <class U>
  static bool compare_vals(bc_opcode op, U lv, U rv) {
    switch (op) {
    case bc_opcode::emit_if_eq:  return lv == rv;
    case bc_opcode::emit_if_ne:  return lv != rv;
    case bc_opcode::emit_if_gt:  return lv > rv;
    case bc_opcode::emit_if_gte: return lv >= rv;
    case bc_opcode::emit_if_lt:  return lv < rv;
    case bc_opcode::emit_if_lte: return lv <= rv;
    default: return false;
    }
  }

  INJAMM_ALWAYS_INLINE static std::expected<void, error_ctx> handle_emit_if_cmp(bc_executor& ex, std::size_t& pc, std::string&) {
    auto const& instr = ex.bc_.instructions[pc];
    auto const& ref   = ex.bc_.var_refs[instr.operand2];
    int rhs = ref.int_filters.empty() ? 0 : ref.int_filters[0].arg;
    bool cond = false;
    auto do_cmp = [&](auto const& field) {
      using FT = std::remove_cvref_t<decltype(field)>;
      if constexpr (std::is_arithmetic_v<FT>) {
        if constexpr (std::floating_point<FT>) {
          /** 実数フィールド: double で比較（long long への切り捨てを回避） */
          cond = compare_vals<double>(instr.op, static_cast<double>(field), static_cast<double>(rhs));
        } else {
          cond = compare_vals<long long>(instr.op, static_cast<long long>(field), static_cast<long long>(rhs));
        }
      } else if constexpr (std::is_enum_v<FT>) {
        /** enum LHS: underlying 整数に変換して算術比較と同じロジックで評価 */
        cond = compare_vals<long long>(instr.op, static_cast<long long>(static_cast<std::underlying_type_t<FT>>(field)), static_cast<long long>(rhs));
      } else if constexpr (std::same_as<FT, std::string> || std::same_as<FT, std::string_view> || char_pointer_v<FT>) {
        if (ref.compare_rhs_kind == compare_operand_kind::string_literal) {
          auto sv = to_sv(field);
          switch (instr.op) {
          case bc_opcode::emit_if_eq: cond = (sv == ref.compare_rhs_text); break;
          case bc_opcode::emit_if_ne: cond = (sv != ref.compare_rhs_text); break;
          default: break;
          }
        }
      }
    };
    if (ref.special == special_var_kind::this_ && ex.loop_) {
      /** ループ要素自身との比較: ループボディ executor の value_ が現在要素 */
      do_cmp(ex.value_);
    } else if (ref.special != special_var_kind::none && ex.loop_) {
      /** loop.index / loop.index1 / loop.size の数値比較（他の loop.* は常に偽） */
      long long lv = 0;
      bool      ok = true;
      switch (ref.special) {
      case special_var_kind::loop_index:  lv = ex.loop_->index; break;
      case special_var_kind::loop_index1: lv = ex.loop_->index + 1; break;
      case special_var_kind::loop_size:   lv = ex.loop_->count; break;
      default: ok = false; break;
      }
      if (ok) { cond = compare_vals<long long>(instr.op, lv, static_cast<long long>(rhs)); }
    } else {
      (void)for_each_field_ref(ex.value_, ref,do_cmp);
    }
    if (!cond) { pc = instr.operand; } else { ++pc; }
    return {};
  }

  INJAMM_ALWAYS_INLINE static std::expected<void, error_ctx> handle_emit_if_logic(bc_executor& ex, std::size_t& pc, std::string&) {
    auto const& instr = ex.bc_.instructions[pc];
    auto const& lhs_ref = ex.bc_.var_refs[instr.operand2];
    bool cond = false;
    bool lhs = eval_var_truthy(ex, lhs_ref);
    if (instr.op == bc_opcode::emit_if_not) {
      cond = !lhs;
    } else {
      auto const& rhs_ref = ex.bc_.var_refs[instr.operand3];
      bool rhs_val = eval_var_truthy(ex, rhs_ref);
      cond = (instr.op == bc_opcode::emit_if_or) ? (lhs || rhs_val) : (lhs && rhs_val);
    }
    if (!cond) { pc = instr.operand; } else { ++pc; }
    return {};
  }

  INJAMM_ALWAYS_INLINE static std::expected<void, error_ctx> handle_emit_else(bc_executor& ex, std::size_t& pc, std::string&) {
    pc = ex.bc_.instructions[pc].operand;
    return {};
  }

  INJAMM_ALWAYS_INLINE static std::expected<void, error_ctx> handle_emit_endif(bc_executor&, std::size_t& pc, std::string&) {
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

  INJAMM_ALWAYS_INLINE static std::expected<void, error_ctx> handle_emit_if_filtered(bc_executor& ex, std::size_t& pc, std::string& filtered) {
    auto const& instr = ex.bc_.instructions[pc];
    bool cond = !filtered.empty() && filtered != "false" && filtered != "0";
    if (!cond) { pc = instr.operand; } else { ++pc; }
    return {};
  }

  static std::expected<void, error_ctx> handle_call_partial(bc_executor& ex, std::size_t& pc, std::string&) {
    auto const& instr = ex.bc_.instructions[pc];
    if (instr.operand >= ex.bc_.partial_entries.size()) {
      return std::unexpected(error_ctx{.position = pc, .ec = error_code::syntax_error});
    }
    auto const& entry = ex.bc_.partial_entries[instr.operand];
    if (!entry.bc) {
      return std::unexpected(error_ctx{.position = pc, .ec = error_code::syntax_error});
    }
    bc_executor<T, RootT, Sink> child_exec(*entry.bc, ex.value_, ex.root_value_, ex.loop_, ex.out_);
    auto r = child_exec.execute();
    if (!r)
      return std::unexpected(r.error());
    ++pc;
    return {};
  }

public:
  bc_executor(bytecode const& bc, T const& value, RootT const& root_value, bc_loop_state const* loop, Sink& out) : bc_(bc), value_(value), root_value_(root_value), loop_(loop), out_(out) {}

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
                if (!ref.is_loop_parent || !resolve_loop_parent_var(*this, ref.special, raw)) {
                  auto r = for_each_field_ref(value_, ref,
                    [&](auto const& field) { emit_var_value(field, raw); });
                  if (!r) return r;
                }
                break;
              }
              case bc_opcode::halt:
                return {};
              default:
                // is_simple 保証の破れ。黙って出力を打ち切らずエラーを返す。
                return std::unexpected(error_ctx{.position = i, .ec = error_code::syntax_error});
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
        &&L_emit_literal,
        &&L_emit_var,
        &&L_emit_var_raw,
        &&L_emit_section,
        &&L_emit_end,
        &&L_emit_inverted,
        &&L_emit_at_index,
        &&L_emit_at_first,
        &&L_emit_at_last,
        &&L_emit_if,
        &&L_emit_if_cmp,
        &&L_emit_if_cmp,
        &&L_emit_if_cmp,
        &&L_emit_if_cmp,
        &&L_emit_if_cmp,
        &&L_emit_if_cmp,
        &&L_emit_else,
        &&L_emit_endif,
        &&L_emit_at_section,
        &&L_emit_at_inverted,
        &&L_emit_litvar,
        &&L_emit_litvar_raw,
        &&L_emit_at_root,
        &&L_emit_at_root_field,
        &&L_emit_at_root_field_raw,
        &&L_emit_at_key,
        &&L_emit_this,
        &&L_resolve_filtered,
        &&L_filter_string,
        &&L_filter_int,
        &&L_filter_float,
        &&L_emit_filtered,
        &&L_emit_filtered_raw,
        &&L_emit_if_filtered,
        &&L_emit_break,
        &&L_emit_continue,
        &&L_emit_at_index1,
        &&L_emit_at_size,
        &&L_emit_var_size,
        &&L_emit_if_logic,
        &&L_emit_if_logic,
        &&L_emit_if_not,
        &&L_call_partial,
        &&L_halt,
    };
    static_assert(std::size(dispatch_table) == static_cast<std::size_t>(bc_opcode::halt) + 1, "dispatch_table size mismatch");

/** @brief 現在の命令のオペコードに対応するラベルにジャンプする（実行範囲外なら終了） */
#define DISPATCH()                                                         \
  do {                                                                     \
    if (pc >= end)                                                         \
      goto L_halt;                                                         \
    auto _op = static_cast<unsigned>(bc_.instructions[pc].op);             \
    if (_op >= std::size(dispatch_table))                                  \
      return std::unexpected(error_ctx{.position = pc, .ec = error_code::syntax_error}); \
    goto* dispatch_table[_op];                                             \
  } while (0)

// ponytail: 各ラベルの本体を handle_* 関数（switch フォールバックと共用）に委譲する。
// 間接分岐の goto* は DISPATCH に残るため computed goto の恩恵は保持され、本体は
// インライン展開（同一 TU の static メンバ）により直書きラベルと同等のコードになる。
#define HANDLE(fn)                                                         \
  do {                                                                     \
    if (auto _r = fn(*this, pc, filtered_value_); !_r)                     \
      return _r;                                                           \
  } while (0)

    if (pc >= end)
      goto L_halt;
    DISPATCH();

  L_emit_literal: { HANDLE(handle_emit_literal); DISPATCH(); }

  L_emit_var:
  L_emit_var_raw: { HANDLE(handle_emit_var); DISPATCH(); }

  L_emit_section: { HANDLE(handle_emit_section); DISPATCH(); }

  L_emit_end: { HANDLE(handle_emit_end); DISPATCH(); }

  L_emit_inverted: { HANDLE(handle_emit_inverted); DISPATCH(); }

  L_emit_at_index: { HANDLE(handle_emit_at_index); DISPATCH(); }

  L_emit_at_index1: { HANDLE(handle_emit_at_index1); DISPATCH(); }

  L_emit_at_size: { HANDLE(handle_emit_at_size); DISPATCH(); }

  L_emit_var_size: { HANDLE(handle_emit_var_size); DISPATCH(); }

  L_emit_at_first: { HANDLE(handle_emit_at_first); DISPATCH(); }

  L_emit_at_last: { HANDLE(handle_emit_at_last); DISPATCH(); }

  L_emit_if: { HANDLE(handle_emit_if); DISPATCH(); }

  L_emit_if_cmp: { HANDLE(handle_emit_if_cmp); DISPATCH(); }

  L_emit_if_not: { HANDLE(handle_emit_if_logic); DISPATCH(); }

  L_emit_if_logic: { HANDLE(handle_emit_if_logic); DISPATCH(); }

  L_emit_else: { HANDLE(handle_emit_else); DISPATCH(); }

  L_emit_endif: { HANDLE(handle_emit_endif); DISPATCH(); }

  L_emit_at_section: { HANDLE(handle_emit_at_section); DISPATCH(); }

  L_emit_at_inverted: { HANDLE(handle_emit_at_inverted); DISPATCH(); }

  L_emit_litvar:
  L_emit_litvar_raw: { HANDLE(handle_emit_litvar); DISPATCH(); }

  L_emit_at_root: { HANDLE(handle_emit_at_root); DISPATCH(); }

  L_emit_at_root_field:
  L_emit_at_root_field_raw: { HANDLE(handle_emit_at_root_field); DISPATCH(); }

  L_emit_at_key: { HANDLE(handle_emit_at_key); DISPATCH(); }

  L_emit_this: { HANDLE(handle_emit_this); DISPATCH(); }

  L_resolve_filtered: { HANDLE(handle_resolve_filtered); DISPATCH(); }

  L_filter_string: { HANDLE(handle_filter_string); DISPATCH(); }

  L_filter_int: { HANDLE(handle_filter_int); DISPATCH(); }

  L_filter_float: { HANDLE(handle_filter_float); DISPATCH(); }

  L_emit_filtered: { HANDLE(handle_emit_filtered); DISPATCH(); }

  L_emit_filtered_raw: { HANDLE(handle_emit_filtered); DISPATCH(); }

  L_emit_if_filtered: { HANDLE(handle_emit_if_filtered); DISPATCH(); }

  L_emit_break: { HANDLE(handle_emit_break); return {}; }

  L_emit_continue: { HANDLE(handle_emit_continue); return {}; }

  L_call_partial: { HANDLE(handle_call_partial); DISPATCH(); }

  /** @brief プログラム終端 */
  L_halt: { return {}; }

#undef DISPATCH
#undef HANDLE

#else
    /**
     * Clang 等 computed goto 非対応コンパイラ向けフォールバック。
     * 関数ポインタテーブルではなく switch による直接呼び出しにすることで、
     * コンパイラのジャンプテーブル生成とハンドラのインライン展開を可能にする。
     */
    while (pc < end) {
      std::expected<void, error_ctx> r;
      switch (bc_.instructions[pc].op) {
      case bc_opcode::emit_literal:           r = handle_emit_literal(*this, pc, filtered_value_); break;
      case bc_opcode::emit_var:
      case bc_opcode::emit_var_raw:           r = handle_emit_var(*this, pc, filtered_value_); break;
      case bc_opcode::emit_section:           r = handle_emit_section(*this, pc, filtered_value_); break;
      case bc_opcode::emit_end:               r = handle_emit_end(*this, pc, filtered_value_); break;
      case bc_opcode::emit_inverted:          r = handle_emit_inverted(*this, pc, filtered_value_); break;
      case bc_opcode::emit_at_index:          r = handle_emit_at_index(*this, pc, filtered_value_); break;
      case bc_opcode::emit_at_first:          r = handle_emit_at_first(*this, pc, filtered_value_); break;
      case bc_opcode::emit_at_last:           r = handle_emit_at_last(*this, pc, filtered_value_); break;
      case bc_opcode::emit_if:                r = handle_emit_if(*this, pc, filtered_value_); break;
      case bc_opcode::emit_if_eq:
      case bc_opcode::emit_if_ne:
      case bc_opcode::emit_if_gt:
      case bc_opcode::emit_if_gte:
      case bc_opcode::emit_if_lt:
      case bc_opcode::emit_if_lte:            r = handle_emit_if_cmp(*this, pc, filtered_value_); break;
      case bc_opcode::emit_else:              r = handle_emit_else(*this, pc, filtered_value_); break;
      case bc_opcode::emit_endif:             r = handle_emit_endif(*this, pc, filtered_value_); break;
      case bc_opcode::emit_at_section:        r = handle_emit_at_section(*this, pc, filtered_value_); break;
      case bc_opcode::emit_at_inverted:       r = handle_emit_at_inverted(*this, pc, filtered_value_); break;
      case bc_opcode::emit_litvar:
      case bc_opcode::emit_litvar_raw:        r = handle_emit_litvar(*this, pc, filtered_value_); break;
      case bc_opcode::emit_at_root:           r = handle_emit_at_root(*this, pc, filtered_value_); break;
      case bc_opcode::emit_at_root_field:
      case bc_opcode::emit_at_root_field_raw: r = handle_emit_at_root_field(*this, pc, filtered_value_); break;
      case bc_opcode::emit_at_key:            r = handle_emit_at_key(*this, pc, filtered_value_); break;
      case bc_opcode::emit_this:              r = handle_emit_this(*this, pc, filtered_value_); break;
      case bc_opcode::resolve_filtered:       r = handle_resolve_filtered(*this, pc, filtered_value_); break;
      case bc_opcode::filter_string:          r = handle_filter_string(*this, pc, filtered_value_); break;
      case bc_opcode::filter_int:             r = handle_filter_int(*this, pc, filtered_value_); break;
      case bc_opcode::filter_float:           r = handle_filter_float(*this, pc, filtered_value_); break;
      case bc_opcode::emit_filtered:
      case bc_opcode::emit_filtered_raw:      r = handle_emit_filtered(*this, pc, filtered_value_); break;
      case bc_opcode::emit_if_filtered:       r = handle_emit_if_filtered(*this, pc, filtered_value_); break;
      case bc_opcode::emit_break:             r = handle_emit_break(*this, pc, filtered_value_); break;
      case bc_opcode::emit_continue:          r = handle_emit_continue(*this, pc, filtered_value_); break;
      case bc_opcode::emit_at_index1:         r = handle_emit_at_index1(*this, pc, filtered_value_); break;
      case bc_opcode::emit_at_size:           r = handle_emit_at_size(*this, pc, filtered_value_); break;
      case bc_opcode::emit_var_size:          r = handle_emit_var_size(*this, pc, filtered_value_); break;
      case bc_opcode::emit_if_or:
      case bc_opcode::emit_if_and:
      case bc_opcode::emit_if_not:            r = handle_emit_if_logic(*this, pc, filtered_value_); break;
      case bc_opcode::call_partial:           r = handle_call_partial(*this, pc, filtered_value_); break;
      case bc_opcode::halt:                   r = handle_emit_halt(*this, pc, filtered_value_); break;
      default:
        return std::unexpected(error_ctx{.position = pc, .ec = error_code::syntax_error});
      }
      if (!r) return std::unexpected(r.error());
    }
    return {};
#endif
  }

private:
};

/**
 * @brief 出力バッファのサイズ見積もりを計算する
 * @tparam T コンテキスト値の型
 * @param bc バイトコード
 * @param value コンテキスト値
 * @return 推定出力サイズ
 */
template <class T>
std::size_t estimate_output_size(bytecode const& bc, T const&) {
  return bc.literal_total_size * 4 + bc.var_refs.size() * 32;
}

/**
 * @brief バイトコードを実行してレンダリング結果を取得する
 * @tparam T コンテキスト値の型
 * @param bc バイトコード
 * @param value コンテキスト値
 * @return std::expected<std::string, error_ctx> レンダリング結果
 */
template <class T>
std::expected<std::string, error_ctx> bc_execute(bytecode const& bc, T const& value, std::size_t size_hint) {
  if (bc.error.ec != error_code::none)
    return std::unexpected(bc.error);
  std::string out;
  auto        estimated = estimate_output_size(bc, value);
  /** 前回レンダリングの実測サイズ（engine が渡す）を優先して再確保を防ぐ */
  if (size_hint > estimated) estimated = size_hint;
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

/**
 * @brief バイトコードを実行して任意の sink に出力する（コールバック/ストリーミング用）
 * @tparam T    コンテキスト値の型
 * @tparam Sink detail::output_sink を満たす出力先（append(std::string_view) と
 *              append(char const*, std::size_t) を持つ。std::string も可）
 * @param bc    バイトコード
 * @param value コンテキスト値
 * @param sink  出力先 sink（内容はクリアされない）
 * @return std::expected<void, error_ctx> 実行結果
 */
template <class T, class Sink>
  requires output_sink<Sink>
std::expected<void, error_ctx> bc_execute_into_sink(bytecode const& bc, T const& value, Sink& sink) {
  bc_executor<T, T, Sink> exec(bc, value, value, nullptr, sink);
  return exec.execute();
}

}  // namespace injamm::detail
