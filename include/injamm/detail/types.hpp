#pragma once

#include <cstddef>
#include <ostream>
#include <string_view>

namespace injamm {

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
};

/** @brief error_code をストリームに出力するためのオーバーロード */
inline std::ostream& operator<<(std::ostream& os, error_code ec) {
  return os << static_cast<int>(ec);
}

/** @brief エラーコンテキスト
 *
 *  エラー発生位置とエラーコード、カスタムメッセージを保持する。
 */
struct error_ctx {
  std::size_t position{};                   /**< エラー発生位置（バイトオフセット） */
  error_code ec{error_code::none};          /**< エラーコード */
  std::string_view custom_error_message;    /**< カスタムエラーメッセージ */
};

/** @brief レンダリングモードタグ: エスケープなし（デフォルト） */
struct stencil_tag {};

/** @brief レンダリングモードタグ: HTMLエスケープ有効 */
struct mustache_tag {};

inline constexpr stencil_tag stencil_v{};
inline constexpr mustache_tag mustache_v{};

} // namespace injamm
