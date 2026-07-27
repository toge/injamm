#pragma once
#ifndef RENDER_RENDER8_HPP
#define RENDER_RENDER8_HPP
/**
 * @file render.hpp
 * @brief injamm_codegen によって自動生成されたレンダリング関数
 */

#include <expected>
#include <string>

#include <injamm/types.hpp>
#include <injamm/escape.hpp>
#include "codegen_helpers.hpp"

namespace generated {


/**
 * @brief テンプレート文字列から生成されたレンダリング関数
 *
 * @details injamm_codegen によって自動生成された関数。
 *          テンプレート引数 T は data.name, data.age 等の
 *          フィールドにアクセス可能な型でなければならない。
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
render8(const T& data) {
  std::string out;
  out.reserve(1);
  
  for (std::size_t _i1 = 0; _i1 < data.items.size(); ++_i1) {
    const auto& _item1 = data.items[_i1];
    html_escape_append_value(out, data.name);
    out += ":";
    html_escape_append_value(out, _item1.name);
  }
  
  return out;
}

} // namespace generated

#endif // RENDER_RENDER8_HPP
