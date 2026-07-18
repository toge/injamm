#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <ostream>
#include <string_view>

#if __has_include(<frozenchars/mod/core.hpp>)
#include <frozenchars/mod/core.hpp>
#ifndef INJAMM_HAS_FROZENCHARS
#define INJAMM_HAS_FROZENCHARS 1
#endif
#endif

namespace injamm {

/** @brief loop 変数の種別（loop.index / loop.index1 / loop.size / loop.is_first / loop.is_last / loop.key） */
enum class at_var_kind : std::uint8_t { index, index1, size, first, last, key };


/** @brief エラーコード
 *
 *  テンプレートレンダリング中に発生する様々なエラーを識別するための列挙型。
 */
enum class error_code : int {
  none             = 0, /**< エラーなし */
  no_read_input    = 1, /**< 入力が空 */
  unexpected_end   = 2, /**< 予期しないテンプレート終端 */
  unknown_key      = 3, /**< 不明なキー */
  syntax_error     = 4, /**< 構文エラー */
  type_mismatch    = 5, /**< 型不一致 */
  invalid_utf8     = 6, /**< 不正な UTF-8 */
  unknown_filter   = 7, /**< 不明なフィルタ名 */
  division_by_zero = 8, /**< 除数ゼロエラー */
};

/** @brief error_code から対応するエラーメッセージを取得する */
inline std::string_view error_code_to_message(error_code ec) {
  switch (ec) {
  case error_code::none:
    return "No error";
  case error_code::no_read_input:
    return "No input provided";
  case error_code::unexpected_end:
    return "Unexpected end of template";
  case error_code::unknown_key:
    return "Unknown key or field name";
  case error_code::syntax_error:
    return "Template syntax error";
  case error_code::type_mismatch:
    return "Type mismatch";
  case error_code::invalid_utf8:
    return "Invalid UTF-8 sequence";
  case error_code::unknown_filter:
    return "Unknown filter name";
  case error_code::division_by_zero:
    return "Division by zero";
  default:
    return "Unknown error";
  }
}

/** @brief error_code をストリームに出力するためのオーバーロード */
inline std::ostream& operator<<(std::ostream& os, error_code ec) {
  return os << error_code_to_message(ec);
}

/** @brief エラーコンテキスト
 *
 *  エラー発生位置とエラーコード、カスタムメッセージを保持する。
 */
struct error_ctx {
  std::size_t      position{};           /**< エラー発生位置（バイトオフセット） */
  error_code       ec{error_code::none}; /**< エラーコード */
  std::string_view custom_error_message; /**< カスタムエラーメッセージ */

  /** @brief エラーメッセージを生成する */
  [[nodiscard]] std::string message() const {
    if (!custom_error_message.empty()) {
      return std::string(custom_error_message);
    }
    return std::string(error_code_to_message(ec));
  }

  /** @brief エラーが発生しているか判定する */
  [[nodiscard]] bool has_error() const noexcept { return ec != error_code::none; }
};

/** @brief レンダリングモードタグ: エスケープなし（デフォルト） */
struct stencil_tag {};

/** @brief レンダリングモードタグ: HTMLエスケープ有効 */
struct mustache_tag {};

inline constexpr stencil_tag  stencil_v{};
inline constexpr mustache_tag mustache_v{};

/** @brief 結果型エイリアス
 *
 *  テンプレートレンダリングの戻り値型。
 *  成功時は T、失敗時は error_ctx を保持する。
 *
 *  @tparam T 成功時の値の型
 */
template <class T>
using expected = std::expected<T, error_ctx>;

/** @brief NTTP コンパイル時文字列（ヌル終端を含む長さ N） */
template <std::size_t N>
struct fixed_string {
  char data[N]{}; /**< @brief 内部バッファ（ヌル終端文字列） */

  fixed_string() = default;

  /** @brief 文字列リテラルから構築する */
  consteval fixed_string(char const (&str)[N]) {
    for (std::size_t i = 0; i < N; ++i) {
      data[i] = str[i];
    }
  }

  /** @brief 文字列長を返す（ヌル終端除く） */
  [[nodiscard]] constexpr std::size_t size() const noexcept {
    std::size_t len = 0;
    while (len < N && data[len] != '\0')
      ++len;
    return len;
  }

#if INJAMM_HAS_FROZENCHARS
  /** @brief frozenchars::FrozenString から構築する（NTTP 変換用）
   *
   *  explicit にすることで、_fs リテラル（FrozenString）が render の
   *  fixed_string オーバーロードへ暗黙変換・推定されるのを防ぐ。
   *  FrozenString は auto NTTP オーバーロード側で直接扱われる。 */
  explicit constexpr fixed_string(frozenchars::FrozenString<N> const& fs) {
    for (std::size_t i = 0; i < N; ++i) {
      data[i] = fs.data()[i];
    }
  }
#endif
};

// ponytail: クラステンプレート推定ガイド。injamm::fixed_string("...") で N を推定可能に。
template <std::size_t N>
fixed_string(char const (&str)[N]) -> fixed_string<N>;

#if INJAMM_HAS_FROZENCHARS
// FrozenString からの推定（minify_html 等のパイプ結果を fixed_string に構築する場合）。
template <std::size_t N>
fixed_string(frozenchars::FrozenString<N> const&) -> fixed_string<N>;
#endif

}  // namespace injamm
