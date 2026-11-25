#pragma once
#ifndef INJAMM_CODEGEN_NS_67656E657261746564_FN_72656E6465725F67_HPP
#define INJAMM_CODEGEN_NS_67656E657261746564_FN_72656E6465725F67_HPP
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

namespace cgns_render_g {
struct control_slot {
  bool break_requested = false;
  bool continue_requested = false;
};
} // namespace cgns_render_g

namespace cgns_render_g {
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
} // namespace cgns_render_g


/**
 * @brief 名前付き partial をレンダリングする内部実装
 *
 * @details 値 (value) とルート (root) を分けて受け取り、本体は
 *          「値 → ルート」の二段解決で生成される（VM handle_call_partial と同一）。
 *          ループ内から呼ばれたグローバル partial も現在の要素を優先して解決する。
 */
template <injamm::fixed_string PartialName, typename V, typename R, typename Sink>
  requires injamm::detail::output_sink<Sink>
[[nodiscard]] std::expected<void, injamm::error_ctx>
render_g_partial_impl(const V& value, const R& root, const codegen_loop_context& _loop, cgns_render_g::control_slot& _root_control, Sink& out);

template <injamm::fixed_string PartialName, typename V, typename R, typename Sink>
  requires injamm::detail::output_sink<Sink>
[[nodiscard]] std::expected<void, injamm::error_ctx>
render_g_partial_impl(const V& value, const R& root, const codegen_loop_context& _loop, cgns_render_g::control_slot& _root_control, Sink& out) {
  if constexpr (std::string_view(PartialName.data) == "g") {
    html_escape_append_value(out, cgns_render_g::cg_get_6E616D65(value, root));
    out.append(";");
    return {};
}
  else {
    return std::unexpected(injamm::error_ctx{.ec = injamm::error_code::unknown_key, .custom_error_message = "unknown partial: " + std::string(PartialName.data)});
}
}

struct render_g_partial_tag {};

/**
 * @brief 名前付き partial を NTTP で指定してレンダリングする
 *
 * @details injamm_codegen によって自動生成された関数。
 *          PartialName に partial 名を NTTP 文字列として渡す。
 *          存在しない名前の場合は unknown_key エラーを返す。
 *
 * @tparam T          データ型
 * @tparam PartialName partial 名（fixed_string 互換 NTTP）
 * @tparam Sink       出力先型（std::string または output_sink を満たす型）
 * @param data レンダリング対象のデータ
 * @param out  出力先バッファ（内容は追記される）
 * @return 正常時: void。エラー時: error_ctx
 *
 * @code
 *   // 使い方例:
 *   std::string out;
 *   auto result = generated::render_partial<"header">(data, out);
 * @endcode
 */
template <injamm::fixed_string PartialName, typename T, typename Sink = std::string>
  requires injamm::detail::output_sink<Sink> && (std::string_view(PartialName.data) == "g")
        && std::same_as<render_g_partial_tag, render_g_partial_tag>
[[nodiscard]] std::expected<void, injamm::error_ctx>
render_partial(const T& data, Sink& out) {
  cgns_render_g::control_slot _root_control;
  return render_g_partial_impl<PartialName>(data, data, codegen_loop_context{}, _root_control, out);
}

template <injamm::fixed_string PartialName, typename T, typename Sink = std::string>
  requires injamm::detail::output_sink<Sink>
[[nodiscard]] std::expected<void, injamm::error_ctx>
render_g_partial(const T& data, Sink& out) {
  cgns_render_g::control_slot _root_control;
  return render_g_partial_impl<PartialName>(data, data, codegen_loop_context{}, _root_control, out);
}

#ifndef INJAMM_CODEGEN_PARTIAL_FALLBACK_NS_67656E657261746564_HPP
#define INJAMM_CODEGEN_PARTIAL_FALLBACK_NS_67656E657261746564_HPP
template <injamm::fixed_string PartialName, typename T, typename Sink = std::string>
  requires injamm::detail::output_sink<Sink>
[[nodiscard]] std::expected<void, injamm::error_ctx>
render_partial(const T&, Sink&) {
  return std::unexpected(injamm::error_ctx{.ec = injamm::error_code::unknown_key, .custom_error_message = "unknown partial: " + std::string(PartialName.data)});
}
#endif // INJAMM_CODEGEN_PARTIAL_FALLBACK_NS_67656E657261746564_HPP


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
render_g(const T& data, Sink& out) {
  if constexpr (std::is_same_v<Sink, std::string>) {
    out.clear();
  }
  cgns_render_g::control_slot _root_control;

  auto _body1 = [&](const auto& _item1, const codegen_loop_context& _loop1, cgns_render_g::control_slot& _control1) {
    auto _partial_result2 = render_g_partial_impl<"g">(_item1, data, _loop1, _control1, out);
    if (!_partial_result2) return;
    if (_control1.break_requested || _control1.continue_requested) {
      return;
    }
  };
  if constexpr (codegen_unsupported_string_v<decltype(data.items)>) {
    static_assert(!codegen_unsupported_string_v<decltype(data.items)>, "raw char arrays and volatile C strings are not supported in codegen sections");
  } else if constexpr (codegen_string_section_v<decltype(data.items)>) {
    if (!value_empty(data.items)) {
      cgns_render_g::control_slot _loop_control1 = {};
      _body1(data, codegen_loop_context{true, 0, 0}, _loop_control1);
    }
  } else if constexpr (codegen_optional_section_v<decltype(data.items)>) {
    if (data.items.has_value()) {
      _body1(*data.items, codegen_loop_context{}, _root_control);
      if (_root_control.break_requested || _root_control.continue_requested) {
        return {};
      }
    }
  } else if constexpr (codegen_scalar_section_v<decltype(data.items)>) {
    if (static_cast<bool>(data.items)) {
      _body1(data, codegen_loop_context{}, _root_control);
      if (_root_control.break_requested || _root_control.continue_requested) {
        return {};
      }
    }
  } else {
    cgns_render_g::control_slot _loop_control1;
    auto _size1 = data.items.size();
    for (std::size_t _i1 = 0; _i1 < _size1; ++_i1) {
      _loop_control1 = {};
      _body1(data.items[_i1], codegen_loop_context{true, _i1, _size1}, _loop_control1);
      if (_loop_control1.break_requested) break;
      if (_loop_control1.continue_requested) continue;
    }
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
render_g(const T& data) {
  std::string out;
  auto result = render_g(data, out);
  if (!result) return std::unexpected(result.error());
  return out;
}

} // namespace generated

#endif // INJAMM_CODEGEN_NS_67656E657261746564_FN_72656E6465725F67_HPP
