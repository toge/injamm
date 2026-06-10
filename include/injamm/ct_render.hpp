#pragma once

#include "types.hpp"
#include "ct_chunk.hpp"
#include "escape.hpp"
#include "filters.hpp"
#include "loop_state.hpp"
#include "resolve.hpp"
#include "serialize_value.hpp"
#include <array>
#include <charconv>
#include <cmath>
#include <expected>
#include <optional>
#include <glaze/glaze.hpp>
#include <string>
#include <string_view>

namespace injamm::detail {

/**
 * @brief vector-like 型を判定するコンセプト
 * @details `std::string` と `std::string_view` を除外した上で、
 *          `value_type`・`size()`・`operator[]`・イテレータを持つ型を vector-like と見なす。
 *          ループセクションの繰り返し描画に使用される。
 * @tparam T 判定対象の型
 */
template <class T>
concept ct_is_vector_like =
    !std::is_arithmetic_v<T> &&
    !std::same_as<T, std::string> &&
    !std::same_as<T, std::string_view> &&
    requires(T const& v) {
      typename T::value_type;
      { v.size() } -> std::convertible_to<std::size_t>;
      { v[0] };
      { v.begin() };
      { v.end() };
    };

/**
 * @brief map-like コンテナを判定するコンセプト
 * @details `std::map` / `std::unordered_map` 等の連想コンテナを判定する。
 *          `ct_is_vector_like` と排他であり、セクション反復で map 反復に使用される。
 * @tparam T 判定対象の型
 */
template <class T>
concept ct_is_map_like = requires {
  typename T::key_type;
  typename T::mapped_type;
  typename T::value_type;
  { std::declval<T const&>().size() } -> std::convertible_to<std::size_t>;
  { std::declval<T const&>().begin() };
  { std::declval<T const&>().end() };
};

/**
 * @brief set-like コンテナを判定するコンセプト
 * @details `std::set` / `std::multiset` 等の順序付き集合を判定する。
 *          `ct_is_vector_like`（`operator[]` あり）とも `ct_is_map_like`（`key_type`/`mapped_type` あり）とも排他。
 *          セクション反復では各要素を `{{this}}` としてイテレータベースで処理する。
 * @tparam T 判定対象の型
 */
template <class T>
concept ct_is_set_like =
    !std::is_arithmetic_v<T> &&
    !std::same_as<T, std::string> &&
    !std::same_as<T, std::string_view> &&
    requires(T const& v) {
      typename T::value_type;
      { v.size() } -> std::convertible_to<std::size_t>;
      { v.begin() };
      { v.end() };
    } && !requires(T const& v, std::size_t i) {
      { v[i] };
    } && !requires {
      typename T::key_type;
      typename T::mapped_type;
    };

/**
 * @brief glz::meta によるリフレクションが可能な型を判定するコンセプト
 * @details `glz::reflect<T>::size` がコンパイル時に評価できる場合に true となる。
 *          map-like 型は glz::to_tie が使えないため除外する。
 *          セクションやプレースホルダのフィールド解決に使用される。
 * @tparam T 判定対象の型
 */
template <class T>
concept ct_glz_reflectable = requires {
  glz::reflect<T>::size;
} && !is_std_map_like_v<T>;

/**
 * @brief 型 T のフィールド名からインデックスを事前解決する
 * @details チャンク配列内のプレースホルダ / セクション / if のキーを
 *          glz::reflect<T>::keys[] と照合し、一致する field_indices を書き込む。
 *          ドットパスや @ プレフィックスは -1（未解決）のままにする。
 */
template <class T, std::size_t N>
constexpr ct_parsed_template<N> resolve_field_indices(ct_parsed_template<N> tmpl) {
  tmpl.field_indices.fill(-1);
  if constexpr (glz_reflectable<T>) {
    constexpr auto count = glz::reflect<T>::size;
    for (std::size_t i = 0; i < tmpl.size; ++i) {
      auto& idx = tmpl.field_indices[i];
      auto kind = tmpl.kinds[i];
      if (kind != ct_chunk_kind::placeholder && kind != ct_chunk_kind::section &&
          kind != ct_chunk_kind::inverted && kind != ct_chunk_kind::if_else) {
        continue;
      }
      auto key = tmpl.texts[i];
      if (key.empty() || key[0] == '@' || key.find('.') != std::string_view::npos) {
        continue;
      }
      // 線形探索でフィールド名を照合
      [&]<std::size_t... I>(std::index_sequence<I...>) {
        ((std::string_view{glz::reflect<T>::keys[I]} == key && (idx = static_cast<int>(I), true)) || ...);
      }(std::make_index_sequence<count>{});
    }
  }
  return tmpl;
}

/**
 * @brief 事前解決された field_index を使って O(1) でフィールドにアクセスする
 * @details field_index >= 0 の場合、if-else チェーンでコンパイル時インデックスに変換し
 *          glz::get<I>() で直接アクセスする。見つからなければ visitor は呼ばれず false を返す。
 */
template <class T, class F>
constexpr bool with_field_index(T const& value, int field_index, F&& visitor) {
  if constexpr (glz_reflectable<T>) {
    constexpr auto sz = static_cast<std::size_t>(glz::reflect<T>::size);
    if (field_index >= 0 && static_cast<std::size_t>(field_index) < sz) {
      auto tied = glz::to_tie(value);
      bool found = false;
      [&]<std::size_t... I>(std::index_sequence<I...>) {
        ((field_index == static_cast<int>(I) && (found = true, visitor(glz::get<I>(tied)), true)) || ...);
      }(std::make_index_sequence<sz>{});
      return found;
    }
  }
  return false;
}

/**
 * @brief フィールド値をシリアライズし、必要に応じて HTML エスケープする
 * @details mustache_tag + !raw の場合のみエスケープ適用。stencil_tag または raw の場合は生出力。
 *          std::optional は serialize_value のオーバーロードが再帰処理する。
 */
template <class Mode, class Buffer, class FT>
constexpr void serialize_escaped(Buffer& out, FT const& field, bool raw) {
  if constexpr (serializable_v<FT> || is_std_optional_v<FT>) {
    if constexpr (std::is_same_v<Mode, mustache_tag>) {
      if (!raw) {
        std::string tmp;
        serialize_value(tmp, field);
        html_escape_into(out, tmp);
        return;
      }
    }
    serialize_value(out, field);
  }
}

/**
 * @brief 事前解決インデックスを使って O(1) でフィールドをシリアライズする
 * @details ct_render_placeholder の高速パス。フラットキー（@なし、ドットなし）専用。
 */
template <class Mode, class Buffer, class T, std::size_t N>
constexpr bool
ct_render_placeholder_indexed(Buffer& out, ct_parsed_template<N> const& chunks,
                               std::size_t i, T const& value, bool raw) {
  auto fi = chunks.field_indices[i];
  if (fi < 0) return false;
  if constexpr (glz_reflectable<T>) {
    constexpr auto sz = static_cast<std::size_t>(glz::reflect<T>::size);
    if (static_cast<std::size_t>(fi) >= sz) return false;
    auto tied = glz::to_tie(value);
    bool ok = false;
    [&]<std::size_t... I>(std::index_sequence<I...>) {
      ((fi == static_cast<int>(I) && (ok = true, serialize_escaped<Mode>(out, glz::get<I>(tied), raw), true)) || ...);
    }(std::make_index_sequence<sz>{});
    return ok;
  }
  return false;
}

/**
 * @brief ct_render_chunks の前方宣言
 * @details 相互再帰が必要なため、各レンダリング関数よりも先に宣言する。
 */
template <class Mode, class Buffer, class T, class RootT, std::size_t N>
constexpr auto ct_render_chunks(Buffer& out, ct_parsed_template<N> const& chunks, std::size_t start,
                                 std::size_t end, T const& value, RootT const& root_value,
                                 loop_state const* loop) -> std::expected<void, error_ctx>;

/**
 * @brief リテラルテキストをそのまま出力バッファに追加する
 * @tparam Buffer 出力バッファの型（std::string など）
 * @tparam N      チャンク配列のサイズ
 * @param[out] out    出力バッファ
 * @param[in]  chunks チャンク配列
 * @param[in]  i      処理対象チャンクのインデックス
 */
template <class Buffer, std::size_t N>
constexpr void ct_render_literal(Buffer& out, ct_parsed_template<N> const& chunks, std::size_t i) {
  out.append(chunks.texts[i]);
}

/**
 * @brief プレースホルダ（{{var}} / {{{var}}}）をレンダリングする
 * @details キーの種類に応じて以下の分岐を行う:
 *          - `@root.field`: ルートオブジェクトのフィールドを解決
 *          - `@root`: ルートオブジェクト全体をシリアライズ
 *          - `@index` / `@first` / `@last`: ループ状態の値を出力
 *          - 通常キー: 現在のコンテキストからフィールドを解決
 *          mustache モード時は HTML エスケープを適用し、raw モード（{{{}}}）ではエスケープなし。
 * @tparam Mode   レンダリングモード（mustache_tag または stencil_tag）
 * @tparam Buffer 出力バッファの型
 * @tparam T      現在のコンテキストの型
 * @tparam RootT  ルートコンテキストの型
 * @tparam N      チャンク配列のサイズ
 * @param[out] out        出力バッファ
 * @param[in]  chunks     チャンク配列
 * @param[in]  i          処理対象チャンクのインデックス
 * @param[in]  value      現在のコンテキスト値
 * @param[in]  root_value ルートコンテキスト値（@root 参照用）
 * @param[in]  loop       現在のループ状態（@index/@first/@last 参照用、nullptr 可）
 * @return 成功時は void、失敗時は error_ctx
 */
template <class Mode, class Buffer, class T, class RootT, std::size_t N>
constexpr auto ct_render_placeholder(Buffer& out, ct_parsed_template<N> const& chunks, std::size_t i,
                                      T const& value, RootT const& root_value, loop_state const* loop)
    -> std::expected<void, error_ctx> {
  auto const key = chunks.texts[i];
  bool raw = chunks.flags[i] != 0;

  // {{this}}: 現在のコンテキスト自体をシリアライズ
  if (key == "this") {
    if constexpr (serializable_v<T>) {
      if constexpr (std::is_same_v<Mode, mustache_tag>) {
        if (!raw) {
          std::string tmp;
          serialize_value(tmp, value);
          html_escape_into(out, tmp);
          return {};
        }
      }
      serialize_value(out, value);
    } else if constexpr (ct_glz_reflectable<T>) {
      std::string tmp;
      if (auto ec = glz::write_json(value, tmp)) {
        return std::unexpected(error_ctx{.ec = error_code::syntax_error});
      }
      if constexpr (std::is_same_v<Mode, mustache_tag>) {
        if (!raw) {
          html_escape_into(out, tmp);
          return {};
        }
      }
      out.append(tmp);
    }
    return {};
  }

  auto fcnt = chunks.filter_count[i];
  auto ifcnt = chunks.int_filter_count[i];
  auto ffcnt = chunks.float_filter_count[i];

  // フィルタが存在する場合
  if (fcnt > 0 || ifcnt > 0 || ffcnt > 0) {
    auto const& filters = chunks.filters[i];
    auto const& int_filters = chunks.int_filters[i];
    auto const& float_filters = chunks.float_filters[i];

    std::string tmp;
    if (!resolve_value(tmp, key, value, loop)) {
      return std::unexpected(error_ctx{.ec = error_code::unknown_key});
    }
    // 文字列フィルタ適用
    for (std::size_t j = 0; j < fcnt; ++j) {
      apply_string_filter(tmp, filters[j]);
    }
    // 整数フィルタ適用
    for (std::size_t j = 0; j < ifcnt; ++j) {
      if (auto err = apply_int_filter(tmp, int_filters[j]); !err) {
        return std::unexpected(err.error());
      }
    }
    // 実数フィルタ適用
    for (std::size_t j = 0; j < ffcnt; ++j) {
      apply_float_filter(tmp, float_filters[j]);
    }
    // 出力
    if constexpr (std::is_same_v<Mode, mustache_tag>) {
      if (!raw) {
        html_escape_into(out, std::string_view{tmp});
      } else {
        out.append(tmp);
      }
    } else {
      out.append(tmp);
    }
    return {};
  }

  /**
   * @brief `@root.field` 形式の処理
   * @details `@root.` プレフィックスでルートオブジェクトのフィールドを直接参照する。
   *          ルートコンテキストが現在のコンテキストと異なる場合に有用。
   */
  if (key.starts_with("@root.")) {
    auto const rest = key.substr(6);
    if constexpr (std::is_same_v<Mode, mustache_tag>) {
      if (!raw) {
        std::string tmp;
        if (!resolve_value(tmp, rest, root_value, nullptr)) {
          return std::unexpected(error_ctx{.ec = error_code::unknown_key});
        }
        html_escape_into(out, std::string_view{tmp});
        return {};
      }
    }
    if (!resolve_value(out, rest, root_value, nullptr)) {
      return std::unexpected(error_ctx{.ec = error_code::unknown_key});
    }
    return {};
  }

  /**
   * @brief `@root` 単体の処理
   * @details ルートオブジェクト全体を JSON 形式などでシリアライズして出力する。
   *          ルート型がシリアライズ可能でない場合はエラーを返す。
   */
  if (key == "@root") {
    if constexpr (serializable_v<RootT>) {
      serialize_value(out, root_value);
    } else {
      return std::unexpected(error_ctx{.ec = error_code::type_mismatch});
    }
    return {};
  }

  /**
   * @brief @index / @first / @last の処理
   * @details ループ状態から対応する値を出力する。
   *          ループ外で参照された場合は何も出力しない（空文字）。
   */
  if (key.starts_with("@")) {
    if (!loop) {
      return {};
    }
    if (key == "@index") {
      serialize_value(out, loop->index);
      return {};
    }
    if (key == "@first") {
      serialize_value(out, loop->is_first());
      return {};
    }
    if (key == "@last") {
      serialize_value(out, loop->is_last());
      return {};
    }
    return {};
  }

  /**
   * @brief 通常のフィールド解決
   * @details フラットキー（ドットなし、@なし）は事前解決済みインデックスで O(1) アクセス。
   *          それ以外は resolve_value にフォールバックする。
   */
  if (key.find('.') == std::string_view::npos && (key.empty() || key[0] != '@')) {
    if (ct_render_placeholder_indexed<Mode>(out, chunks, i, value, raw)) {
      return {};
    }
  }
  if constexpr (std::is_same_v<Mode, mustache_tag>) {
    if (!raw) {
      std::string tmp;
      if (!resolve_value(tmp, key, value, loop)) {
        return std::unexpected(error_ctx{.ec = error_code::unknown_key});
      }
      html_escape_into(out, std::string_view{tmp});
      return {};
    }
  }
  if (!resolve_value(out, key, value, loop)) {
    return std::unexpected(error_ctx{.ec = error_code::unknown_key});
  }
  return {};
}

/**
 * @brief セクション（{{#key}}...{{/key}}）をレンダリングする
 * @details キーに対応するフィールドの型に応じて動作が分岐する:
 *          - vector-like 型: 各要素に対して本体を繰り返し描画（ループ）
 *          - bool 型: true の場合に本体を 1 回描画
 *          - その他の型: type_mismatch エラー
 * @tparam Mode   レンダリングモード
 * @tparam Buffer 出力バッファの型
 * @tparam T      現在のコンテキストの型
 * @tparam RootT  ルートコンテキストの型
 * @tparam N      チャンク配列のサイズ
 * @param[out] out          出力バッファ
 * @param[in]  chunks       チャンク配列
 * @param[in]  i            処理対象チャンクのインデックス
 * @param[in]  value        現在のコンテキスト値
 * @param[in]  root_value   ルートコンテキスト値
 * @param[in]  parent_loop  親ループのループ状態（ネスト時継承用）
 * @return 成功時は void、失敗時は error_ctx
 */
template <class Mode, class Buffer, class T, class RootT, std::size_t N>
constexpr auto ct_render_section(Buffer& out, ct_parsed_template<N> const& chunks, std::size_t i,
                                  T const& value, RootT const& root_value, loop_state const* parent_loop)
    -> std::expected<void, error_ctx> {
  auto const key = chunks.texts[i];
  auto body_start = chunks.body_starts[i];
  auto body_end = chunks.body_ends[i];

  if constexpr (ct_glz_reflectable<T>) {
    constexpr auto sz = static_cast<std::size_t>(glz::reflect<T>::size);
    bool found = false;
    std::expected<void, error_ctx> res{};
    auto tied = glz::to_tie(value);
    /**
     * @brief フィールド検索と処理を行う内部ラムダ
     * @details 一致したフィールドの型に応じて vector-like（ループ）/ bool（条件分岐）/ optional を処理。
     */
    auto handle_field = [&](auto const& field) {
      using FT = std::remove_cvref_t<decltype(field)>;
      found = true;
      if constexpr (ct_is_vector_like<FT>) {
        loop_state ls;
        ls.count = static_cast<std::uint32_t>(field.size());
        ls.in_loop = true;
        for (ls.index = 0; ls.index < static_cast<std::uint32_t>(field.size()); ++ls.index) {
          res = ct_render_chunks<Mode>(out, chunks, body_start, body_end, field[ls.index], root_value, &ls);
          if (!res) return;
          if (ls.continue_flag) {
            ls.continue_flag = false;
            continue;
          }
          if (ls.break_flag) break;
        }
      } else if constexpr (std::same_as<FT, bool>) {
        if (field) {
          res = ct_render_chunks<Mode>(out, chunks, body_start, body_end, value, root_value, parent_loop);
        }
      } else if constexpr (is_std_optional_v<FT>) {
        if (field.has_value()) {
          res = ct_render_chunks<Mode>(out, chunks, body_start, body_end, *field, root_value, parent_loop);
        }
      } else if constexpr (ct_is_map_like<FT>) {
        /** map の場合: キーを @key として各要素をループ */
        loop_state ls;
        ls.count = static_cast<std::uint32_t>(field.size());
        ls.in_loop = true;
        for (auto const& [k, v] : field) {
          ls.key = std::string_view{k};
          res = ct_render_chunks<Mode>(out, chunks, body_start, body_end, v, root_value, &ls);
          if (!res) return;
          if (ls.continue_flag) {
            ls.continue_flag = false;
            continue;
          }
          if (ls.break_flag) break;
          ++ls.index;
        }
      } else if constexpr (ct_is_set_like<FT>) {
        /** set の場合: 各要素を {{this}} としてイテレータベースでループ */
        loop_state ls;
        ls.count = static_cast<std::uint32_t>(field.size());
        ls.in_loop = true;
        for (auto const& elem : field) {
          res = ct_render_chunks<Mode>(out, chunks, body_start, body_end, elem, root_value, &ls);
          if (!res) return;
          if (ls.continue_flag) {
            ls.continue_flag = false;
            continue;
          }
          if (ls.break_flag) break;
          ++ls.index;
        }
      } else if constexpr (ct_glz_reflectable<FT>) {
        constexpr auto sz2 = glz::reflect<FT>::size;
        auto tied2 = glz::to_tie(field);
        [&]<std::size_t... J>(std::index_sequence<J...>) {
          (([&] {
            if (!res) return;
            loop_state ls;
            ls.count = sz2;
            ls.index = J;
            ls.key = glz::reflect<FT>::keys[J];
            ls.in_loop = true;
            res = ct_render_chunks<Mode>(out, chunks, body_start, body_end, glz::get<J>(tied2), root_value, &ls);
          }()), ...);
        }(std::make_index_sequence<sz2>{});
      } else {
        res = std::unexpected(error_ctx{.ec = error_code::type_mismatch});
      }
    };

    /**
     * @brief 事前解決インデックスによる O(1) アクセス
     */
    {
      auto fi = chunks.field_indices[i];
      if (fi >= 0 && static_cast<std::size_t>(fi) < sz) {
        [&]<std::size_t... I>(std::index_sequence<I...>) {
          (([&] {
            if (found) return;
            if (fi != static_cast<int>(I)) return;
            handle_field(glz::get<I>(tied));
          }()), ...);
        }(std::make_index_sequence<sz>{});
      }
    }

    /**
     * @brief フォールバック: 線形探索（キー名照合）
     */
    if (!found) {
      [&]<std::size_t... I>(std::index_sequence<I...>) {
        (([&] {
          if (found) return;
          if (std::string_view{glz::reflect<T>::keys[I]} != key) return;
          handle_field(glz::get<I>(tied));
        }()), ...);
      }(std::make_index_sequence<sz>{});
    }

    if (!found) {
      return std::unexpected(error_ctx{.ec = error_code::unknown_key});
    }
    return res;
  } else {
    return std::unexpected(error_ctx{.ec = error_code::type_mismatch});
  }
}

/**
 * @brief 逆セクション（{{^key}}...{{/key}}）をレンダリングする
 * @details キーに対応するフィールドの型に応じて動作が分岐する:
 *          - vector-like 型: 空の場合に本体を描画
 *          - bool 型: false の場合に本体を描画
 *          - その他の型: type_mismatch エラー
 *          セクション（{{#key}}）の真偽が逆転した動作。
 * @tparam Mode   レンダリングモード
 * @tparam Buffer 出力バッファの型
 * @tparam T      現在のコンテキストの型
 * @tparam RootT  ルートコンテキストの型
 * @tparam N      チャンク配列のサイズ
 * @param[out] out          出力バッファ
 * @param[in]  chunks       チャンク配列
 * @param[in]  i            処理対象チャンクのインデックス
 * @param[in]  value        現在のコンテキスト値
 * @param[in]  root_value   ルートコンテキスト値
 * @param[in]  parent_loop  親ループのループ状態
 * @return 成功時は void、失敗時は error_ctx
 */
template <class Mode, class Buffer, class T, class RootT, std::size_t N>
constexpr auto ct_render_inverted(Buffer& out, ct_parsed_template<N> const& chunks, std::size_t i,
                                   T const& value, RootT const& root_value, loop_state const* parent_loop)
    -> std::expected<void, error_ctx> {
  auto const key = chunks.texts[i];
  auto body_start = chunks.body_starts[i];
  auto body_end = chunks.body_ends[i];

  if constexpr (ct_glz_reflectable<T>) {
    constexpr auto sz = static_cast<std::size_t>(glz::reflect<T>::size);
    bool found = false;
    std::expected<void, error_ctx> res{};
    auto tied = glz::to_tie(value);
    /**
     * @brief 逆セクション用フィールド処理ラムダ
     * @details vector-like（空 → 描画）/ bool（false → 描画）/ optional（空 → 描画）
     *          / 構造体（全フィールド反復）を処理。
     */
    auto handle_inverted = [&](auto const& field) {
      using FT = std::remove_cvref_t<decltype(field)>;
      found = true;
      if constexpr (ct_is_vector_like<FT>) {
        if (field.empty()) {
          res = ct_render_chunks<Mode>(out, chunks, body_start, body_end, value, root_value, parent_loop);
        }
      } else if constexpr (std::same_as<FT, bool>) {
        if (!field) {
          res = ct_render_chunks<Mode>(out, chunks, body_start, body_end, value, root_value, parent_loop);
        }
      } else if constexpr (is_std_optional_v<FT>) {
        if (!field.has_value()) {
          res = ct_render_chunks<Mode>(out, chunks, body_start, body_end, value, root_value, parent_loop);
        }
      } else if constexpr (ct_is_map_like<FT>) {
        if (field.empty()) {
          res = ct_render_chunks<Mode>(out, chunks, body_start, body_end, value, root_value, parent_loop);
        }
      } else if constexpr (ct_is_set_like<FT>) {
        if (field.empty()) {
          res = ct_render_chunks<Mode>(out, chunks, body_start, body_end, value, root_value, parent_loop);
        }
      } else if constexpr (ct_glz_reflectable<FT>) {
        constexpr auto sz2 = glz::reflect<FT>::size;
        auto tied2 = glz::to_tie(field);
        [&]<std::size_t... J>(std::index_sequence<J...>) {
          (([&] {
            if (!res) return;
            loop_state ls;
            ls.count = sz2;
            ls.index = J;
            ls.key = glz::reflect<FT>::keys[J];
            ls.in_loop = true;
            res = ct_render_chunks<Mode>(out, chunks, body_start, body_end, glz::get<J>(tied2), root_value, &ls);
          }()), ...);
        }(std::make_index_sequence<sz2>{});
      } else {
        res = std::unexpected(error_ctx{.ec = error_code::type_mismatch});
      }
    };

    /**
     * @brief 事前解決インデックスによる O(1) アクセス
     */
    {
      auto fi = chunks.field_indices[i];
      if (fi >= 0 && static_cast<std::size_t>(fi) < sz) {
        [&]<std::size_t... I>(std::index_sequence<I...>) {
          (([&] {
            if (found) return;
            if (fi != static_cast<int>(I)) return;
            handle_inverted(glz::get<I>(tied));
          }()), ...);
        }(std::make_index_sequence<sz>{});
      }
    }

    /**
     * @brief フォールバック: 線形探索（キー名照合）
     */
    if (!found) {
      [&]<std::size_t... I>(std::index_sequence<I...>) {
        (([&] {
          if (found) return;
          if (std::string_view{glz::reflect<T>::keys[I]} != key) return;
          handle_inverted(glz::get<I>(tied));
        }()), ...);
      }(std::make_index_sequence<sz>{});
    }

    if (!found) {
      return std::unexpected(error_ctx{.ec = error_code::unknown_key});
    }
    return res;
  } else {
    return std::unexpected(error_ctx{.ec = error_code::type_mismatch});
  }
}

/**
 * @brief @var タグ（{{@index}} / {{@first}} / {{@last}} / {{@root}}）をレンダリングする
 * @details 単体の @var チャンクを処理し、対応する値を出力する。
 *          @index / @first / @last はループ状態が nullptr の場合は何も出力しない。
 * @tparam Buffer 出力バッファの型
 * @tparam T      現在のコンテキストの型
 * @tparam RootT  ルートコンテキストの型
 * @tparam N      チャンク配列のサイズ
 * @param[out] out        出力バッファ
 * @param[in]  chunks     チャンク配列
 * @param[in]  i          処理対象チャンクのインデックス
 * @param[in]  value      現在のコンテキスト値（@var では未使用）
 * @param[in]  root_value ルートコンテキスト値（@root 用）
 * @param[in]  loop       現在のループ状態（nullptr 可）
 * @return 成功時は void、失敗時は error_ctx
 */
template <class Buffer, class T, class RootT, std::size_t N>
constexpr auto ct_render_at_var(Buffer& out, ct_parsed_template<N> const& chunks, std::size_t i,
                                 T const& /*value*/, RootT const& root_value, loop_state const* loop)
    -> std::expected<void, error_ctx> {
  auto var = static_cast<ct_at_var_kind>(chunks.flags[i]);
  switch (var) {
    case ct_at_var_kind::index:
      if (!loop) return {};
      serialize_value(out, loop->index);
      break;
    case ct_at_var_kind::first:
      if (!loop) return {};
      serialize_value(out, loop->is_first());
      break;
    case ct_at_var_kind::last:
      if (!loop) return {};
      serialize_value(out, loop->is_last());
      break;
    case ct_at_var_kind::root:
      if constexpr (serializable_v<RootT>) {
        serialize_value(out, root_value);
      } else {
        return std::unexpected(error_ctx{.ec = error_code::type_mismatch});
      }
      break;
    case ct_at_var_kind::key:
      if (!loop) return {};
      if (!loop->key.empty()) {
        serialize_value(out, loop->key);
      } else {
        serialize_value(out, loop->index);
      }
      break;
  }
  return {};
}

/**
 * @brief @var セクション（{{#@first}}...{{/@first}} / {{^@last}}...{{/@last}}）をレンダリングする
 * @details ループ内の位置に応じて条件判定を行い、本体を描画する。
 *          inverted フラグが true の場合は条件が反転する。
 *          各 @var 種別の条件:
 *          - index: loop->index > 0（先頭要素以外）
 *          - first: loop->is_first()（先頭要素）
 *          - last:  loop->is_last()（末尾要素）
 *          - root:  常に false（実質的には使われない）
 * @tparam Mode   レンダリングモード
 * @tparam Buffer 出力バッファの型
 * @tparam T      現在のコンテキストの型
 * @tparam RootT  ルートコンテキストの型
 * @tparam N      チャンク配列のサイズ
 * @param[out] out        出力バッファ
 * @param[in]  chunks     チャンク配列
 * @param[in]  i          処理対象チャンクのインデックス
 * @param[in]  value      現在のコンテキスト値
 * @param[in]  root_value ルートコンテキスト値
 * @param[in]  loop       現在のループ状態（nullptr 可）
 * @return 成功時は void、失敗時は error_ctx
 */
template <class Mode, class Buffer, class T, class RootT, std::size_t N>
constexpr auto ct_render_at_section(Buffer& out, ct_parsed_template<N> const& chunks, std::size_t i,
                                     T const& value, RootT const& root_value, loop_state const* loop)
    -> std::expected<void, error_ctx> {
  auto var = static_cast<ct_at_var_kind>(chunks.flags[i]);
  auto body_start = chunks.body_starts[i];
  auto body_end = chunks.body_ends[i];
  bool inverted = chunks.else_starts[i] != 0;

  bool cond = false;
  if (loop) {
    switch (var) {
      case ct_at_var_kind::last:
        cond = loop->is_last();
        break;
      case ct_at_var_kind::first:
        cond = loop->is_first();
        break;
      case ct_at_var_kind::index:
        cond = loop->index > 0;
        break;
      case ct_at_var_kind::root:
        cond = false;
        break;
    }
  }
  /** @brief 逆セクション（{{^@var}}）の場合は条件を反転 */
  if (inverted) {
    cond = !cond;
  }
  if (cond) {
    auto r = ct_render_chunks<Mode>(out, chunks, body_start, body_end, value, root_value, loop);
    if (!r) return r;
  }
  return {};
}

/**
 * @brief if 条件式を型ベースで評価するヘルパー
 * @details フィルタなし・@変数なしの通常条件式を Glaze リフレクションの型に応じて評価する。
 *          vector → empty チェック、bool → 真偽値、数値 → 0 チェック、
 *          optional → has_value チェック、文字列 → resolve_value 経由。
 */
template <class T>
constexpr bool evaluate_if_expr_impl(std::string_view expr, T const& value) {
  if constexpr (ct_glz_reflectable<T>) {
    bool cond = false;
    constexpr auto count = static_cast<std::size_t>(glz::reflect<T>::size);
    auto tied = glz::to_tie(value);
    [&]<std::size_t... I>(std::index_sequence<I...>) {
      (([&] {
        if (cond) return;
        if (std::string_view{glz::reflect<T>::keys[I]} != expr) return;
        auto const& field = glz::get<I>(tied);
        using FT = std::remove_cvref_t<decltype(field)>;
        if constexpr (ct_is_vector_like<FT>) {
          cond = !field.empty();
        } else if constexpr (ct_is_set_like<FT>) {
          cond = !field.empty();
        } else if constexpr (std::same_as<FT, bool>) {
          cond = field;
        } else if constexpr (std::is_arithmetic_v<FT>) {
          cond = (field != 0);
        } else if constexpr (is_std_optional_v<FT>) {
          cond = field.has_value();
        } else if constexpr (serializable_v<FT>) {
          std::string tmp;
          serialize_value(tmp, field);
          cond = !tmp.empty() && tmp != "false" && tmp != "0";
        }
      }()), ...);
    }(std::make_index_sequence<count>{});
    return cond;
  } else {
    std::string tmp;
    if (resolve_value(tmp, expr, value, nullptr)) {
      return !tmp.empty() && tmp != "false" && tmp != "0";
    }
    return false;
  }
}

/**
 * @brief if/else 条件分岐（{{#if X}}...{{else}}...{{/if}}）をレンダリングする
 * @details 条件式 X が真の場合に then 節、偽の場合に else 節を描画する。
 *          else 節が存在しない場合は本体のみとなる。
 *          条件式が `@` で始まる場合はループ状態（@last / @first / @index）を参照。
 *          通常の文字列条件は resolve_value で解決し、空文字・"false"・"0" 以外を真とする。
 * @tparam Mode   レンダリングモード
 * @tparam Buffer 出力バッファの型
 * @tparam T      現在のコンテキストの型
 * @tparam RootT  ルートコンテキストの型
 * @tparam N      チャンク配列のサイズ
 * @param[out] out        出力バッファ
 * @param[in]  chunks     チャンク配列
 * @param[in]  i          処理対象チャンクのインデックス
 * @param[in]  value      現在のコンテキスト値
 * @param[in]  root_value ルートコンテキスト値
 * @param[in]  loop       現在のループ状態
 * @return 成功時は void、失敗時は error_ctx
 */
template <class Mode, class Buffer, class T, class RootT, std::size_t N>
constexpr auto ct_render_if(Buffer& out, ct_parsed_template<N> const& chunks, std::size_t i,
                             T const& value, RootT const& root_value, loop_state const* loop)
    -> std::expected<void, error_ctx> {
  bool cond = false;
  auto const expr = chunks.texts[i];

  auto fcnt = chunks.filter_count[i];
  auto ifcnt = chunks.int_filter_count[i];
  auto ffcnt = chunks.float_filter_count[i];

  /** フィルタがある場合は先に値を解決してフィルタを適用する */
  if (fcnt > 0 || ifcnt > 0 || ffcnt > 0) {
    auto const& filters = chunks.filters[i];
    auto const& int_filters = chunks.int_filters[i];
    auto const& float_filters = chunks.float_filters[i];

    std::string tmp;
    if (resolve_value(tmp, expr, value, loop)) {
      for (std::size_t j = 0; j < fcnt; ++j) {
        apply_string_filter(tmp, filters[j]);
      }
      for (std::size_t j = 0; j < ifcnt; ++j) {
        if (auto err = apply_int_filter(tmp, int_filters[j]); !err) {
          return std::unexpected(err.error());
        }
      }
      for (std::size_t j = 0; j < ffcnt; ++j) {
        apply_float_filter(tmp, float_filters[j]);
      }
      cond = !tmp.empty() && tmp != "false" && tmp != "0";
    }
  } else if (!expr.empty() && expr[0] == '@') {
    /**
     * @brief `@` で始まる条件式の処理
     * @details @last / @first / @index はループ状態から直接判定する。
     *          ループ外ではすべて偽となる。
     */
    if (loop) {
      if (expr == "@last") cond = loop->is_last();
      else if (expr == "@first") cond = loop->is_first();
      else if (expr == "@index") cond = loop->index > 0;
    }
  } else {
    cond = evaluate_if_expr_impl<T>(expr, value);
  }

  auto then_start = chunks.body_starts[i];
  auto then_end = chunks.body_ends[i];
  auto else_start = chunks.else_starts[i];
  auto else_end = chunks.else_ends[i];

  /** @brief 条件に応じて then 節または else 節の範囲を選択 */
  auto [start, end] = cond ? std::pair{then_start, then_end} : std::pair{else_start, else_end};
  auto r = ct_render_chunks<Mode>(out, chunks, start, end, value, root_value, loop);
  if (!r) return r;
  return {};
}

/**
 * @brief フラットなチャンク列を指定範囲に従って順次レンダリングする（SoA 版ディスパッチャ）
 * @details start から end までの各チャンクを種類に応じて適切なレンダリング関数に dispatch する。
 *          チャンクの種類:
 *          - literal:    そのまま出力
 *          - placeholder: 変数解決とエスケープ
 *          - section:     ループまたは条件付き描画
 *          - inverted:    逆条件描画
 *          - at_var:      @index/@first/@last/@root の値出力
 *          - at_section:   @var 条件セクション
 *          - if_else:     if/else 条件分岐
 * @tparam Mode   レンダリングモード
 * @tparam Buffer 出力バッファの型
 * @tparam T      現在のコンテキストの型
 * @tparam RootT  ルートコンテキストの型
 * @tparam N      チャンク配列のサイズ
 * @param[out] out        出力バッファ
 * @param[in]  chunks     チャンク配列
 * @param[in]  start      レンダリング開始インデックス
 * @param[in]  end        レンダリング終了インデックス（この手前まで）
 * @param[in]  value      現在のコンテキスト値
 * @param[in]  root_value ルートコンテキスト値
 * @param[in]  loop       現在のループ状態（nullptr 可）
 * @return 成功時は void、失敗時は error_ctx
 */
template <class Mode, class Buffer, class T, class RootT, std::size_t N>
constexpr auto ct_render_chunks(Buffer& out, ct_parsed_template<N> const& chunks, std::size_t start,
                                 std::size_t end, T const& value, RootT const& root_value,
                                 loop_state const* loop) -> std::expected<void, error_ctx> {
  for (std::size_t i = start; i < end; ++i) {
    if (loop && loop->continue_flag) return {};
    switch (chunks.kinds[i]) {
      case ct_chunk_kind::literal:
        ct_render_literal(out, chunks, i);
        break;
      case ct_chunk_kind::placeholder: {
        auto r = ct_render_placeholder<Mode>(out, chunks, i, value, root_value, loop);
        if (!r) return r;
        break;
      }
      case ct_chunk_kind::section: {
        auto r = ct_render_section<Mode>(out, chunks, i, value, root_value, loop);
        if (!r) return r;
        i = chunks.body_ends[i] - 1; // skip body chunks (already rendered recursively)
        break;
      }
      case ct_chunk_kind::inverted: {
        auto r = ct_render_inverted<Mode>(out, chunks, i, value, root_value, loop);
        if (!r) return r;
        i = chunks.body_ends[i] - 1;
        break;
      }
      case ct_chunk_kind::at_var: {
        auto r = ct_render_at_var(out, chunks, i, value, root_value, loop);
        if (!r) return r;
        break;
      }
      case ct_chunk_kind::at_section: {
        auto r = ct_render_at_section<Mode>(out, chunks, i, value, root_value, loop);
        if (!r) return r;
        i = chunks.body_ends[i] - 1;
        break;
      }
      case ct_chunk_kind::if_else: {
        auto r = ct_render_if<Mode>(out, chunks, i, value, root_value, loop);
        if (!r) return r;
        auto skip_to = chunks.else_ends[i] > chunks.body_ends[i] ? chunks.else_ends[i] : chunks.body_ends[i];
        i = skip_to - 1;
        break;
      }
      case ct_chunk_kind::ct_break: {
        if (loop) {
          loop->break_flag = true;
        }
        return {};
      }
      case ct_chunk_kind::ct_continue: {
        if (loop) {
          loop->continue_flag = true;
        }
        return {};
      }
    }
  }
  return {};
}

} // namespace injamm::detail
