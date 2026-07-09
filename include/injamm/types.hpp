#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <ostream>
#include <string_view>

namespace injamm {

/** @brief loop 変数の種別（loop.index / loop.index1 / loop.size / loop.is_first / loop.is_last / loop.key） */
enum class at_var_kind : std::uint8_t { index, index1, size, first, last, key };


/** @brief エラーコード
 *
 *  テンプレートレンダリング中に発生する様々なエラーを識別するための列挙型。
 */
enum class error_code : int {
  none = 0,             /**< エラーなし */
  no_read_input = 1,    /**< 入力が空 */
  unexpected_end = 2,   /**< 予期しないテンプレート終端 */
  unknown_key = 3,      /**< 不明なキー */
  syntax_error = 4,     /**< 構文エラー */
  type_mismatch = 5,    /**< 型不一致 */
  invalid_utf8 = 6,     /**< 不正な UTF-8 */
  unknown_filter = 7,      /**< 不明なフィルタ名 */
  division_by_zero = 8,    /**< 除数ゼロエラー */
};

/** @brief error_code から対応するエラーメッセージを取得する */
inline std::string_view error_code_to_message(error_code ec) {
  switch (ec) {
    case error_code::none: return "No error";
    case error_code::no_read_input: return "No input provided";
    case error_code::unexpected_end: return "Unexpected end of template";
    case error_code::unknown_key: return "Unknown key or field name";
    case error_code::syntax_error: return "Template syntax error";
    case error_code::type_mismatch: return "Type mismatch";
    case error_code::invalid_utf8: return "Invalid UTF-8 sequence";
    case error_code::unknown_filter: return "Unknown filter name";
    case error_code::division_by_zero: return "Division by zero";
    default: return "Unknown error";
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
  std::size_t position{};                   /**< エラー発生位置（バイトオフセット） */
  error_code ec{error_code::none};          /**< エラーコード */
  std::string_view custom_error_message;    /**< カスタムエラーメッセージ */

  /** @brief エラーメッセージを生成する */
  [[nodiscard]] std::string message() const {
    if (!custom_error_message.empty()) {
      return std::string(custom_error_message);
    }
    return std::string(error_code_to_message(ec));
  }

  /** @brief エラーが発生しているか判定する */
  [[nodiscard]] bool has_error() const noexcept {
    return ec != error_code::none;
  }
};

/** @brief レンダリングモードタグ: エスケープなし（デフォルト） */
struct stencil_tag {};

/** @brief レンダリングモードタグ: HTMLエスケープ有効 */
struct mustache_tag {};

inline constexpr stencil_tag stencil_v{};
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

} // namespace injamm
