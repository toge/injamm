#pragma once

#include "bytecode.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string_view>

namespace injamm::detail {

constexpr std::size_t max_filters_per_chunk = 4;

enum class ct_chunk_kind : std::uint8_t { literal, placeholder, section, inverted, at_var, at_section, if_else, ct_break, ct_continue, partial_ref };

/** @brief @変数種別は types.hpp の at_var_kind を使用 */

/**
 * @brief SoA（Struct of Arrays）形式のコンパイル時パース結果
 *
 * @details テンプレート引数 N で最大チャンク数を指定する。
 *          個別のチャンクオブジェクトを動的確保せず、全要素を並列配列で保持するため
 *          キャッシュ効率が良く、constexpr コンテキストでの構築が容易。
 *
 * @tparam N 最大チャンク数
 */
template <std::size_t N>
struct ct_parsed_template {
  std::array<ct_chunk_kind, N>                                          kinds{};               /**< @brief 各チャンクの種別 */
  std::array<std::string_view, N>                                       texts{};               /**< @brief リテラル文字列または変数キー */
  std::array<std::size_t, N>                                            body_starts{};         /**< @brief セクション本体の開始インデックス */
  std::array<std::size_t, N>                                            body_ends{};           /**< @brief セクション本体の終了インデックス */
  std::array<std::size_t, N>                                            else_starts{};         /**< @brief else 節の開始インデックス（if_else 用） */
  std::array<std::size_t, N>                                            else_ends{};           /**< @brief else 節の終了インデックス（if_else 用） */
  std::array<std::uint8_t, N>                                           flags{};               /**< @brief 汎用フラグ（raw / kind / inverted の兼用） */
  std::array<std::array<string_filter_entry, max_filters_per_chunk>, N> filters{};             /**< @brief 各プレースホルダに適用する文字列フィルタ配列 */
  std::array<std::array<int_filter_entry, max_filters_per_chunk>, N>    int_filters{};         /**< @brief 各プレースホルダに適用する整数フィルタ配列 */
  std::array<std::array<float_filter_entry, max_filters_per_chunk>, N>  float_filters{};       /**< @brief 各プレースホルダに適用する実数フィルタ配列 */
  std::array<std::uint8_t, N>                                           filter_count{};        /**< @brief 文字列フィルタの有効数 */
  std::array<std::uint8_t, N>                                           int_filter_count{};    /**< @brief 整数フィルタの有効数 */
  std::array<std::uint8_t, N>                                           float_filter_count{};  /**< @brief 実数フィルタの有効数 */
  std::array<int, N>                                                    field_indices{};       /**< @brief 事前解決されたフィールドインデックス（-1 = 未解決、resolve_field_indices で fill(-1)） */
  std::array<std::string_view, N>                                       partial_names{};       /**< @brief 事前スキャンされた partial 定義の名前 */
  std::array<std::size_t, N>                                            partial_body_starts{}; /**< @brief partial 本体のテンプレート内開始位置 */
  std::array<std::size_t, N>                                            partial_body_ends{};   /**< @brief partial 本体のテンプレート内終了位置 */
  std::size_t                                                           partial_count{};       /**< @brief 登録された partial 定義の数 */
  std::array<std::string_view, N>                                       compare_rhs_strs{};    /**< @brief if_else の文字列比較 RHS（空 = 不使用） */
  std::size_t                                                           size = 0;              /**< @brief 現在の有効チャンク数 */

  /**
   * @brief リテラルチャンクを追加する
   *
   * @param text リテラルテキスト（string_view）
   */
  constexpr void push_literal(std::string_view text) {
    if (size >= N) {
      throw std::overflow_error("ct_parsed_template: chunk buffer overflow");
    }
    kinds[size] = ct_chunk_kind::literal;
    texts[size] = text;
    ++size;
  }

  /**
   * @brief プレースホルダチャンクを追加する
   *
   * @param key 変数キー
   * @param raw true の場合は HTML エスケープなし（{{{...}}}）
   * @param filter_list 適用する文字列フィルタのリスト
   * @param int_filter_list 適用する整数フィルタのリスト
   */
  constexpr void push_placeholder(std::string_view key, bool raw, std::span<string_filter_entry const> filter_list = {}, std::span<int_filter_entry const> int_filter_list = {},
                                  std::span<float_filter_entry const> float_filter_list = {}) {
    if (size >= N) {
      throw std::overflow_error("ct_parsed_template: chunk buffer overflow");
    }
    kinds[size] = ct_chunk_kind::placeholder;
    texts[size] = key;
    flags[size] = raw ? 1 : 0;
    if (filter_list.size() > max_filters_per_chunk) {
      throw std::overflow_error("injamm: too many string filters per placeholder");
    }
    for (std::size_t j = 0; j < filter_list.size(); ++j)
      filters[size][j] = filter_list[j];
    filter_count[size] = static_cast<std::uint8_t>(filter_list.size());
    if (int_filter_list.size() > max_filters_per_chunk) {
      throw std::overflow_error("injamm: too many int filters per placeholder");
    }
    for (std::size_t j = 0; j < int_filter_list.size(); ++j)
      int_filters[size][j] = int_filter_list[j];
    int_filter_count[size] = static_cast<std::uint8_t>(int_filter_list.size());
    if (float_filter_list.size() > max_filters_per_chunk) {
      throw std::overflow_error("injamm: too many float filters per placeholder");
    }
    for (std::size_t j = 0; j < float_filter_list.size(); ++j)
      float_filters[size][j] = float_filter_list[j];
    float_filter_count[size] = static_cast<std::uint8_t>(float_filter_list.size());
    ++size;
  }

  /**
   * @brief セクションチャンクを追加する
   *
   * @param key セクションキー
   * @param body_start 本体部分の開始インデックス
   * @param body_end   本体部分の終了インデックス
   * @param else_start else 節の開始インデックス（省略時は 0）
   * @param else_end   else 節の終了インデックス（省略時は 0）
   */
  constexpr void push_section(std::string_view key, std::size_t body_start, std::size_t body_end, std::size_t else_start = 0, std::size_t else_end = 0) {
    if (size >= N) {
      throw std::overflow_error("ct_parsed_template: chunk buffer overflow");
    }
    kinds[size]       = ct_chunk_kind::section;
    texts[size]       = key;
    body_starts[size] = body_start;
    body_ends[size]   = body_end;
    else_starts[size] = else_start;
    else_ends[size]   = else_end;
    ++size;
  }

  /**
   * @brief 逆セクションチャンクを追加する
   *
   * @param key セクションキー
   * @param body_start 本体部分の開始インデックス
   * @param body_end   本体部分の終了インデックス
   * @param else_start else 節の開始インデックス（省略時は 0）
   * @param else_end   else 節の終了インデックス（省略時は 0）
   */
  constexpr void push_inverted(std::string_view key, std::size_t body_start, std::size_t body_end, std::size_t else_start = 0, std::size_t else_end = 0) {
    if (size >= N) {
      throw std::overflow_error("ct_parsed_template: chunk buffer overflow");
    }
    kinds[size]       = ct_chunk_kind::inverted;
    texts[size]       = key;
    body_starts[size] = body_start;
    body_ends[size]   = body_end;
    else_starts[size] = else_start;
    else_ends[size]   = else_end;
    ++size;
  }

  /**
   * @brief @変数チャンクを追加する
   *
   * @param var @変数の種別（index / first / last / root）
   */
  constexpr void push_at_var(at_var_kind var) {
    if (size >= N) {
      throw std::overflow_error("ct_parsed_template: chunk buffer overflow");
    }
    kinds[size] = ct_chunk_kind::at_var;
    flags[size] = static_cast<std::uint8_t>(var);
    ++size;
  }

  /**
   * @brief @変数セクションチャンクを追加する
   *
   * @param var        @変数の種別
   * @param body_start 本体部分の開始インデックス
   * @param body_end   本体部分の終了インデックス
   * @param inverted   true の場合は逆セクション（{{^@last}} 相当）
   */
  constexpr void push_at_section(at_var_kind var, std::size_t body_start, std::size_t body_end, bool inverted) {
    if (size >= N) {
      throw std::overflow_error("ct_parsed_template: chunk buffer overflow");
    }
    kinds[size]       = ct_chunk_kind::at_section;
    flags[size]       = static_cast<std::uint8_t>(var);
    body_starts[size] = body_start;
    body_ends[size]   = body_end;
    else_starts[size] = inverted ? 1 : 0; /**< else_starts を inverted フラグとして流用 */
    ++size;
  }

  /**
   * @brief if/else チャンクを追加する
   *
   * @param expr       条件式
   * @param then_start then 節の開始インデックス
   * @param then_end   then 節の終了インデックス
   * @param else_start else 節の開始インデックス（省略時は 0）
   * @param else_end   else 節の終了インデックス（省略時は 0）
   * @param filter_list 文字列フィルタのリスト
   * @param int_filter_list 整数フィルタのリスト
   * @param float_filter_list 実数フィルタのリスト
   */
  constexpr void push_if(std::string_view expr, std::size_t then_start, std::size_t then_end, std::size_t else_start, std::size_t else_end, std::span<string_filter_entry const> filter_list = {},
                         std::span<int_filter_entry const> int_filter_list = {}, std::span<float_filter_entry const> float_filter_list = {}) {
    if (size >= N) {
      throw std::overflow_error("ct_parsed_template: chunk buffer overflow");
    }
    kinds[size]       = ct_chunk_kind::if_else;
    texts[size]       = expr;
    body_starts[size] = then_start;
    body_ends[size]   = then_end;
    else_starts[size] = else_start;
    else_ends[size]   = else_end;
    if (filter_list.size() > max_filters_per_chunk) {
      throw std::overflow_error("injamm: too many string filters per if/else");
    }
    for (std::size_t j = 0; j < filter_list.size(); ++j)
      filters[size][j] = filter_list[j];
    filter_count[size] = static_cast<std::uint8_t>(filter_list.size());
    if (int_filter_list.size() > max_filters_per_chunk) {
      throw std::overflow_error("injamm: too many int filters per if/else");
    }
    for (std::size_t j = 0; j < int_filter_list.size(); ++j)
      int_filters[size][j] = int_filter_list[j];
    int_filter_count[size] = static_cast<std::uint8_t>(int_filter_list.size());
    if (float_filter_list.size() > max_filters_per_chunk) {
      throw std::overflow_error("injamm: too many float filters per if/else");
    }
    for (std::size_t j = 0; j < float_filter_list.size(); ++j)
      float_filters[size][j] = float_filter_list[j];
    float_filter_count[size] = static_cast<std::uint8_t>(float_filter_list.size());
    ++size;
  }

  /**
   * @brief break チャンクを追加する
   */
  constexpr void push_break() {
    if (size >= N) {
      throw std::overflow_error("ct_parsed_template: chunk buffer overflow");
    }
    kinds[size] = ct_chunk_kind::ct_break;
    ++size;
  }

  /**
   * @brief continue チャンクを追加する
   */
  constexpr void push_continue() {
    if (size >= N) {
      throw std::overflow_error("ct_parsed_template: chunk buffer overflow");
    }
    kinds[size] = ct_chunk_kind::ct_continue;
    ++size;
  }

  /**
   * @brief partial 参照チャンクを追加する
   *
   * @param partial_index 事前スキャンされた partial 定義のインデックス
   * @param name          partial 名
   */
  constexpr void push_partial_ref(std::size_t partial_index, std::string_view name) {
    if (size >= N) {
      throw std::overflow_error("ct_parsed_template: chunk buffer overflow");
    }
    kinds[size]       = ct_chunk_kind::partial_ref;
    texts[size]       = name;
    else_starts[size] = partial_index; /**< else_starts を partial インデックスとして流用 */
    ++size;
  }
};

}  // namespace injamm::detail
