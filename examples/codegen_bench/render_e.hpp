#pragma once
#ifndef INJAMM_CODEGEN_NS_67656E657261746564_FN_72656E6465725F65_HPP
#define INJAMM_CODEGEN_NS_67656E657261746564_FN_72656E6465725F65_HPP
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

namespace cgns_render_e {
struct control_slot {
  bool break_requested = false;
  bool continue_requested = false;
};
} // namespace cgns_render_e

namespace cgns_render_e {
/**
 * @brief 値側に @p key が無ければルート側へフォールバックする二段解決ヘルパ
 * @details VM (bytecode_exec.hpp) の value → root 二段走査と同一の解決順。
 */
template <class V, class R>
inline decltype(auto) cg_get_616374697665(V const& v, R const& r) {
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
  cgns_render_e::control_slot _root_control;

  auto _body1 = [&](const auto& _item1, const codegen_loop_context& _loop1, cgns_render_e::control_slot& _control1, const auto& _rt1) {
    auto _body2 = [&](const auto& _item2, const codegen_loop_context& _loop2, cgns_render_e::control_slot& _control2) {
      out.append("Y");
    };
    auto _else3 = [&](cgns_render_e::control_slot& _control_else2) {
      out.append("N");
    };
    if constexpr (codegen_unsupported_string_v<decltype(cgns_render_e::cg_get_616374697665(_item1, data))>) {
      static_assert(!codegen_unsupported_string_v<decltype(cgns_render_e::cg_get_616374697665(_item1, data))>, "raw char arrays and volatile C strings are not supported in codegen sections");
    } else if constexpr (codegen_string_section_v<decltype(cgns_render_e::cg_get_616374697665(_item1, data))>) {
      if (!value_empty(cgns_render_e::cg_get_616374697665(_item1, data))) {
        cgns_render_e::control_slot _loop_control2 = {};
        _body2(_item1, codegen_loop_context{true, 0, 0}, _loop_control2);
      } else {
        _else3(_control1);
        if (_control1.break_requested || _control1.continue_requested) {
          return;
        }
      }
    } else if constexpr (codegen_optional_section_v<decltype(cgns_render_e::cg_get_616374697665(_item1, data))>) {
      if (cgns_render_e::cg_get_616374697665(_item1, data).has_value()) {
        _body2(*cgns_render_e::cg_get_616374697665(_item1, data), _loop1, _control1);
        if (_control1.break_requested || _control1.continue_requested) {
          return;
        }
      } else {
        _else3(_control1);
        if (_control1.break_requested || _control1.continue_requested) {
          return;
        }
      }
    } else if constexpr (codegen_scalar_section_v<decltype(cgns_render_e::cg_get_616374697665(_item1, data))>) {
      if (static_cast<bool>(cgns_render_e::cg_get_616374697665(_item1, data))) {
        _body2(_item1, _loop1, _control1);
        if (_control1.break_requested || _control1.continue_requested) {
          return;
        }
      } else {
        _else3(_control1);
        if (_control1.break_requested || _control1.continue_requested) {
          return;
        }
      }
    } else {
      if (value_empty(cgns_render_e::cg_get_616374697665(_item1, data))) {
        _else3(_control1);
        if (_control1.break_requested || _control1.continue_requested) {
          return;
        }
      } else {
        cgns_render_e::control_slot _loop_control2;
        for (const auto& _v2 : cgns_render_e::cg_get_616374697665(_item1, data)) {
          _loop_control2 = {};
          _body2(_v2, _loop1, _loop_control2);
          if (_loop_control2.break_requested) break;
          if (_loop_control2.continue_requested) continue;
        }
      }
    }
  };
  if constexpr (codegen_unsupported_string_v<decltype(data.items)>) {
    static_assert(!codegen_unsupported_string_v<decltype(data.items)>, "raw char arrays and volatile C strings are not supported in codegen sections");
  } else if constexpr (codegen_string_section_v<decltype(data.items)>) {
    if (!value_empty(data.items)) {
      cgns_render_e::control_slot _loop_control1 = {};
      _body1(data, codegen_loop_context{true, 0, 0}, _loop_control1, data);
    }
  } else if constexpr (codegen_optional_section_v<decltype(data.items)>) {
    if (data.items.has_value()) {
      _body1(*data.items, codegen_loop_context{}, _root_control, data);
      if (_root_control.break_requested || _root_control.continue_requested) {
        return {};
      }
    }
  } else if constexpr (codegen_scalar_section_v<decltype(data.items)>) {
    if (static_cast<bool>(data.items)) {
      _body1(data, codegen_loop_context{}, _root_control, data);
      if (_root_control.break_requested || _root_control.continue_requested) {
        return {};
      }
    }
  } else {
    cgns_render_e::control_slot _loop_control1;
    for (const auto& _v1 : data.items) {
      _loop_control1 = {};
      _body1(_v1, codegen_loop_context{}, _loop_control1, data);
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
render_e(const T& data) {
  std::string out;
  auto result = render_e(data, out);
  if (!result) return std::unexpected(result.error());
  return out;
}

} // namespace generated

#endif // INJAMM_CODEGEN_NS_67656E657261746564_FN_72656E6465725F65_HPP
