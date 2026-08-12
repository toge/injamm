#pragma once
#ifndef RENDER_RENDER16_HPP
#define RENDER_RENDER16_HPP
/**
 * @file render.hpp
 * @brief injamm_codegen によって自動生成されたレンダリング関数
 */

#include <expected>
#include <string>

#include <injamm/types.hpp>
#include <glaze/glaze.hpp>
#include <injamm/escape.hpp>

namespace generated {
#include "codegen_helpers.hpp"


/**
 * @brief テンプレート文字列から生成されたレンダリング関数（バッファ再利用版）
 *
 * @details injamm_codegen によって自動生成された関数。
 *          出力先バッファを引数で受け取り、内部バッファを再利用することで
 *          アロケーションを削減する。
 *
 * @tparam T データ型（フィールドへのアクセスが必要）
 * @param data レンダリング対象のデータ
 * @param out  出力先バッファ（内容はクリアされる）
 * @return 正常時: void。エラー時: error_ctx
 */
template <typename T>
[[nodiscard]] std::expected<void, injamm::error_ctx>
render16(const T& data, std::string& out) {
  out.clear();
  out.reserve(1);
  
  auto _size1 = data.items.size();
  std::size_t _lo1 = 0, _hi1 = _size1;
  bool _bwd1 = false;
  _bwd1 = !_bwd1;
  if (_bwd1) _lo1 = (_hi1 > 100u ? std::max(_lo1, _hi1 - 100u) : _lo1);
  else _hi1 = std::min(_hi1, _lo1 + 100u);
  auto _count1 = _hi1 - _lo1;
  for (std::size_t _i1 = 0; _i1 < _count1; ++_i1) {
    auto _idx1 = _bwd1 ? (_hi1 - 1 - _i1) : (_lo1 + _i1);
    const auto& _item1 = data.items[_idx1];
    html_escape_append_value(out, _item1.name);
    out += ";";
  }
  
  return {};
}

/**
 * @brief テンプレート文字列から生成されたレンダリング関数
 *
 * @details injamm_codegen によって自動生成された関数。
 *          バッファ再利用版 (render(data, out)) のラッパー。
 *
 * @tparam T データ型（フィールドへのアクセスが必要）
 * @param data レンダリング対象のデータ
 * @return 正常時: レンダリング結果文字列。エラー時: error_ctx
 *
 * @code
 *   // 使い方例:
 *   #include "render.hpp"
 *
 *   struct UserData { std::string name; int age; };
 *   UserData user{"Alice", 30};
 *   auto result = generated::render(user);
 *   if (result) std::cout << *result << std::endl;
 * @endcode
 */
template <typename T>
[[nodiscard]] std::expected<std::string, injamm::error_ctx>
render16(const T& data) {
  std::string out;
  auto result = render16(data, out);
  if (!result) return std::unexpected(result.error());
  return out;
}

} // namespace generated

#endif // RENDER_RENDER16_HPP
