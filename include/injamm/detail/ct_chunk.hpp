#pragma once

#include "chunk.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace injamm::detail {

/**
 * @brief コンパイル時チャンク種別
 *
 * @details ct_parsed_template で使用するチャンクの種類を表す列挙型。
 *          実行時の chunk バリアントの全種別をカバーする。
 */
enum class ct_chunk_kind : std::uint8_t {
  literal,      /**< リテラルテキスト */
  placeholder,  /**< プレースホルダ変数 */
  section,      /**< セクション */
  inverted,     /**< 逆セクション */
  at_var,       /**< @変数 */
  at_section,   /**< @変数セクション */
  if_else       /**< if/else */
};

/**
 * @brief @変数の種別（ct_chunk_at_var / ct_chunk_at_section 用）
 *
 * @details ct_parsed_template::flags フィールドに uint8_t として格納される。
 */
enum class ct_at_var_kind : std::uint8_t { index, first, last, root, key };

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
  std::array<ct_chunk_kind, N> kinds{};         /**< @brief 各チャンクの種別 */
  std::array<std::string_view, N> texts{};       /**< @brief リテラル文字列または変数キー */
  std::array<std::size_t, N> body_starts{};      /**< @brief セクション本体の開始インデックス */
  std::array<std::size_t, N> body_ends{};        /**< @brief セクション本体の終了インデックス */
  std::array<std::size_t, N> else_starts{};      /**< @brief else 節の開始インデックス（if_else 用） */
  std::array<std::size_t, N> else_ends{};        /**< @brief else 節の終了インデックス（if_else 用） */
  std::array<std::uint8_t, N> flags{};           /**< @brief 汎用フラグ（raw / kind / inverted の兼用） */
  std::array<std::vector<string_filter_entry>, N> filters{}; /**< @brief 各プレースホルダに適用する文字列フィルタリスト */
  std::array<std::vector<int_filter_entry>, N> int_filters{}; /**< @brief 各プレースホルダに適用する整数フィルタリスト */
  std::size_t size = 0;                          /**< @brief 現在の有効チャンク数 */

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
  constexpr void push_placeholder(std::string_view key, bool raw, std::vector<string_filter_entry> filter_list = {}, std::vector<int_filter_entry> int_filter_list = {}) {
    if (size >= N) {
      throw std::overflow_error("ct_parsed_template: chunk buffer overflow");
    }
    kinds[size] = ct_chunk_kind::placeholder;
    texts[size] = key;
    flags[size] = raw ? 1 : 0;
    filters[size] = std::move(filter_list);
    int_filters[size] = std::move(int_filter_list);
    ++size;
  }

  /**
   * @brief セクションチャンクを追加する
   *
   * @param key セクションキー
   * @param body_start 本体部分の開始インデックス
   * @param body_end   本体部分の終了インデックス
   */
  constexpr void push_section(std::string_view key, std::size_t body_start, std::size_t body_end) {
    if (size >= N) {
      throw std::overflow_error("ct_parsed_template: chunk buffer overflow");
    }
    kinds[size] = ct_chunk_kind::section;
    texts[size] = key;
    body_starts[size] = body_start;
    body_ends[size] = body_end;
    ++size;
  }

  /**
   * @brief 逆セクションチャンクを追加する
   *
   * @param key セクションキー
   * @param body_start 本体部分の開始インデックス
   * @param body_end   本体部分の終了インデックス
   */
  constexpr void push_inverted(std::string_view key, std::size_t body_start, std::size_t body_end) {
    if (size >= N) {
      throw std::overflow_error("ct_parsed_template: chunk buffer overflow");
    }
    kinds[size] = ct_chunk_kind::inverted;
    texts[size] = key;
    body_starts[size] = body_start;
    body_ends[size] = body_end;
    ++size;
  }

  /**
   * @brief @変数チャンクを追加する
   *
   * @param var @変数の種別（index / first / last / root）
   */
  constexpr void push_at_var(ct_at_var_kind var) {
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
  constexpr void push_at_section(ct_at_var_kind var, std::size_t body_start, std::size_t body_end,
                                  bool inverted) {
    if (size >= N) {
      throw std::overflow_error("ct_parsed_template: chunk buffer overflow");
    }
    kinds[size] = ct_chunk_kind::at_section;
    flags[size] = static_cast<std::uint8_t>(var);
    body_starts[size] = body_start;
    body_ends[size] = body_end;
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
   */
  constexpr void push_if(std::string_view expr, std::size_t then_start, std::size_t then_end,
                          std::size_t else_start, std::size_t else_end) {
    if (size >= N) {
      throw std::overflow_error("ct_parsed_template: chunk buffer overflow");
    }
    kinds[size] = ct_chunk_kind::if_else;
    texts[size] = expr;
    body_starts[size] = then_start;
    body_ends[size] = then_end;
    else_starts[size] = else_start;
    else_ends[size] = else_end;
    ++size;
  }
};

} // namespace injamm::detail
