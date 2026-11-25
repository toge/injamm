#pragma once
#ifndef INJAMM_CODEGEN_NS_67656E657261746564_FN_72656E6465725F6A6F696E_HPP
#define INJAMM_CODEGEN_NS_67656E657261746564_FN_72656E6465725F6A6F696E_HPP
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

namespace cgns_render_join {
struct control_slot {
  bool break_requested = false;
  bool continue_requested = false;
};
} // namespace cgns_render_join


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
render_join(const T& data, Sink& out) {
  if constexpr (std::is_same_v<Sink, std::string>) {
    out.clear();
    out.reserve(34);
  }
  cgns_render_join::control_slot _root_control;

  auto _body1 = [&](const auto& _item1, const codegen_loop_context& _loop1, cgns_render_join::control_slot& _control1) {
    html_escape_append_value(out, _item1);
  };
  if constexpr (codegen_unsupported_string_v<decltype(data.sitems)>) {
    static_assert(!codegen_unsupported_string_v<decltype(data.sitems)>, "raw char arrays and volatile C strings are not supported in codegen sections");
  } else if constexpr (codegen_string_section_v<decltype(data.sitems)>) {
    if (!value_empty(data.sitems)) {
      cgns_render_join::control_slot _loop_control1 = {};
      _body1(data, codegen_loop_context{true, 0, 0}, _loop_control1);
    }
  } else if constexpr (codegen_optional_section_v<decltype(data.sitems)>) {
    if (data.sitems.has_value()) {
      _body1(*data.sitems, codegen_loop_context{}, _root_control);
      if (_root_control.break_requested || _root_control.continue_requested) {
        return {};
      }
    }
  } else if constexpr (codegen_scalar_section_v<decltype(data.sitems)>) {
    if (static_cast<bool>(data.sitems)) {
      _body1(data, codegen_loop_context{}, _root_control);
      if (_root_control.break_requested || _root_control.continue_requested) {
        return {};
      }
    }
  } else {
    {
      auto _size1 = data.sitems.size();
      std::size_t _lo1 = 0, _hi1 = _size1;
      bool _bwd1 = false;
      cgns_render_join::control_slot _loop_control1;
      const auto _count1 = _hi1 - _lo1;
      for (std::size_t _i1 = 0; _i1 < _count1; ++_i1) {
        const auto _idx1 = _bwd1 ? (_hi1 - 1 - _i1) : (_lo1 + _i1);
        const auto& _item1 = data.sitems[_idx1];
        const codegen_loop_context _loop1{true, _i1, _count1};
        _loop_control1 = {};
        _body1(_item1, _loop1, _loop_control1);
        if (_loop_control1.break_requested) break;
        if (!_loop_control1.continue_requested && _i1 + 1 < _count1) out.append(", ");
        if (_loop_control1.continue_requested) continue;
      }
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
render_join(const T& data) {
  std::string out;
  auto result = render_join(data, out);
  if (!result) return std::unexpected(result.error());
  return out;
}

} // namespace generated

#endif // INJAMM_CODEGEN_NS_67656E657261746564_FN_72656E6465725F6A6F696E_HPP
