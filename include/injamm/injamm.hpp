#pragma once

#include "detail/types.hpp"
#include <expected>
#include <string>
#include <string_view>

namespace injamm {

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
