#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <ostream>
#include <string_view>

namespace injamm {

/** @brief @変数の種別（@index / @first / @last / @root / @key） */
enum class at_var_kind : std::uint8_t { index, first, last, root, key };


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
