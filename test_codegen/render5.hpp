#pragma once
#ifndef INJAMM_CODEGEN_NS_67656E657261746564_FN_72656E64657235_HPP
#define INJAMM_CODEGEN_NS_67656E657261746564_FN_72656E64657235_HPP
/**
 * @file render.hpp
 * @brief injamm_codegen によって自動生成されたレンダリング関数
 */

#include <expected>
#include <string>

#include <injamm/types.hpp>
#include <glaze/glaze.hpp>
#include <injamm/escape.hpp>
#include "codegen_helpers.hpp"

namespace generated {

namespace cgns_render5 {
/**
 * @brief 値側に @p key が無ければルート側へフォールバックする二段解決ヘルパ
 * @details VM (bytecode_exec.hpp) の value → root 二段走査と同一の解決順。
 */
template <class V, class R>
inline decltype(auto) cg_get_6E616D65(V const& v, R const& r) {
  if constexpr (requires { v.name; }) {
    return (v.name);
  } else {
    return (r.name);
  }
}
/**
 * @brief 値側に @p key が無ければルート側へフォールバックする二段解決ヘルパ
 * @details VM (bytecode_exec.hpp) の value → root 二段走査と同一の解決順。
 */
template <class V, class R>
inline decltype(auto) cg_get_7072696365(V const& v, R const& r) {
  if constexpr (requires { v.price; }) {
    return (v.price);
  } else {
    return (r.price);
  }
}
} // namespace cgns_render5


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
render5(const T& data, Sink& out) {
  if constexpr (std::is_same_v<Sink, std::string>) {
    out.clear();
    out.reserve(159);
  }

  out.append("Order #");
  html_escape_append_value(out, data.order_id);
  out.append(":\n");
  if (data.total > 1000) {
    out.append("[VIP]");
  }
  out.append("\n");
  auto _body1 = [&](const auto& _item1) {
    out.append("\n  ");
    html_escape_append_value(out, cgns_render5::cg_get_6E616D65(_item1, data));
    out.append(": $");
    html_escape_append_value(out, cgns_render5::cg_get_7072696365(_item1, data));
    out.append("\n");
  };
  if constexpr (codegen_optional_section_v<decltype(data.items)>) {
    if (data.items.has_value()) _body1(*data.items);
  } else if constexpr (codegen_scalar_section_v<decltype(data.items)>) {
    if (static_cast<bool>(data.items)) _body1(data);
  } else {
    for (const auto& _v1 : data.items) _body1(_v1);
  }
  out.append("\nTotal: $");
  html_escape_append_value(out, data.total);

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
render5(const T& data) {
  std::string out;
  auto result = render5(data, out);
  if (!result) return std::unexpected(result.error());
  return out;
}

} // namespace generated

#endif // INJAMM_CODEGEN_NS_67656E657261746564_FN_72656E64657235_HPP
