#pragma once
#ifndef RENDER_RENDER_IF_FILTERED_HPP
#define RENDER_RENDER_IF_FILTERED_HPP
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
render_if_filtered(const T& data, Sink& out) {
  if constexpr (std::is_same_v<Sink, std::string>) {
    out.clear();
  }
  std::string _filtered;
  _filtered.reserve(64);
  
  if constexpr (::injamm::detail::serializable_v<decltype(data.age)>) {
    _filtered.clear(); ::injamm::detail::serialize_value(_filtered, data.age);
  } else if constexpr (::injamm::detail::ct_glz_reflectable<decltype(data.age)>) {
    (void)::glz::write_json(data.age, _filtered);
  } else {
    _filtered.assign((data.age).data(), (data.age).size());
  }
  filter_int_is_neg(_filtered);
  if (!_filtered.empty() && _filtered != "false" && _filtered != "0") {
    out.append("YES");
  } else {
    out.append("NO");
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
render_if_filtered(const T& data) {
  std::string out;
  auto result = render_if_filtered(data, out);
  if (!result) return std::unexpected(result.error());
  return out;
}

} // namespace generated

#endif // RENDER_RENDER_IF_FILTERED_HPP
