#pragma once
#ifndef RENDER_RENDER11_HPP
#define RENDER_RENDER11_HPP
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
 *          出力先を引数で受け取る。std::string ならバッファを再利用し、
 *          injamm::detail::output_sink を満たす型（callback_sink 等）なら
 *          ストリーミング出力する。
 *
 * @tparam T    データ型（フィールドへのアクセスが必要）
 * @tparam Sink 出力先型（std::string または output_sink を満たす型）
 * @param data レンダリング対象のデータ
 * @param out  出力先（std::string なら内容はクリアされる）
 * @return 正常時: void。エラー時: error_ctx
 */
template <typename T, typename Sink = std::string>
  requires injamm::detail::output_sink<Sink>
[[nodiscard]] std::expected<void, injamm::error_ctx>
render11(const T& data, Sink& out) {
  if constexpr (std::is_same_v<Sink, std::string>) {
    out.clear();
    out.reserve(8);
  }
  
  auto _size1 = data.items.size();
  for (std::size_t _i1 = 0; _i1 < _size1; ++_i1) {
    const auto& _item1 = data.items[_i1];
    out.append("[");
    if (_i1 == 0) {
      out.append("F:");
      html_escape_append_value(out, _item1.name);
    }
    out.append("][");
    if (_i1 > 0) {
      out.append("N:");
      html_escape_append_value(out, _item1.name);
    }
    out.append("]");
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
render11(const T& data) {
  std::string out;
  auto result = render11(data, out);
  if (!result) return std::unexpected(result.error());
  return out;
}

} // namespace generated

#endif // RENDER_RENDER11_HPP
