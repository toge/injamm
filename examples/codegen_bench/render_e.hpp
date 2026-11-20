#pragma once
#ifndef RENDER_RENDER_E_HPP
#define RENDER_RENDER_E_HPP
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

namespace cgns_render_e {
/**
 * @brief 値側に @p key が無ければルート側へフォールバックする二段解決ヘルパ
 * @details VM (bytecode_exec.hpp) の value → root 二段走査と同一の解決順。
 */
template <class V, class R>
inline decltype(auto) cg_get_active(V const& v, R const& r) {
  if constexpr (requires { v.active; }) {
    return (v.active);
  } else {
    return (r.active);
  }
}
} // namespace cgns_render_e


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
render_e(const T& data, Sink& out) {
  if constexpr (std::is_same_v<Sink, std::string>) {
    out.clear();
  }
  
  auto _body1 = [&](const auto& _item1, const auto& _rt1) {
    auto _body2 = [&](const auto& _item2) {
      out.append("Y");
    };
    auto _else3 = [&]() {
      out.append("N");
    };
    if constexpr (requires { _item1.active; }) {
      if constexpr (codegen_optional_section_v<decltype(_item1.active)>) {
        if (_item1.active.has_value()) _body2(*_item1.active); else _else3();
      } else if constexpr (codegen_scalar_section_v<decltype(_item1.active)>) {
        if (static_cast<bool>(_item1.active)) _body2(_item1); else _else3();
      } else {
        if (value_empty(_item1.active)) _else3(); else for (const auto& _v2 : _item1.active) _body2(_v2);
      }
    } else {
      if constexpr (codegen_optional_section_v<decltype(_rt1.active)>) {
        if (_rt1.active.has_value()) _body2(*_rt1.active); else _else3();
      } else if constexpr (codegen_scalar_section_v<decltype(_rt1.active)>) {
        if (static_cast<bool>(_rt1.active)) _body2(_item1); else _else3();
      } else {
        if (value_empty(_rt1.active)) _else3(); else for (const auto& _v2 : _rt1.active) _body2(_v2);
      }
    }
  };
  if constexpr (codegen_optional_section_v<decltype(data.items)>) {
    if (data.items.has_value()) _body1(*data.items, data);
  } else if constexpr (codegen_scalar_section_v<decltype(data.items)>) {
    if (static_cast<bool>(data.items)) _body1(data, data);
  } else {
    for (const auto& _v1 : data.items) _body1(_v1, data);
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
render_e(const T& data) {
  std::string out;
  auto result = render_e(data, out);
  if (!result) return std::unexpected(result.error());
  return out;
}

} // namespace generated

#endif // RENDER_RENDER_E_HPP
