#pragma once
#ifndef RENDER_RENDER18_HPP
#define RENDER_RENDER18_HPP
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
render18(const T& data, Sink& out) {
  if constexpr (std::is_same_v<Sink, std::string>) {
    out.clear();
    out.reserve(10);
  }
  std::string _filtered;
  _filtered.reserve(64);
  
  if constexpr (::injamm::detail::serializable_v<decltype(data.order_id)>) {
    _filtered.clear(); ::injamm::detail::serialize_value(_filtered, data.order_id);
  } else if constexpr (::injamm::detail::ct_glz_reflectable<decltype(data.order_id)>) {
    (void)::glz::write_json(data.order_id, _filtered);
  } else {
    _filtered.assign(data.order_id);
  }
  filter_int_numify(_filtered);
  html_escape_append(out, _filtered);
  out.append("|");
  if constexpr (::injamm::detail::serializable_v<decltype(data.order_id)>) {
    _filtered.clear(); ::injamm::detail::serialize_value(_filtered, data.order_id);
  } else if constexpr (::injamm::detail::ct_glz_reflectable<decltype(data.order_id)>) {
    (void)::glz::write_json(data.order_id, _filtered);
  } else {
    _filtered.assign(data.order_id);
  }
  filter_int_zerofill(_filtered, 7);
  html_escape_append(out, _filtered);
  out.append("|");
  if constexpr (::injamm::detail::serializable_v<decltype(data.email)>) {
    _filtered.clear(); ::injamm::detail::serialize_value(_filtered, data.email);
  } else if constexpr (::injamm::detail::ct_glz_reflectable<decltype(data.email)>) {
    (void)::glz::write_json(data.email, _filtered);
  } else {
    _filtered.assign(data.email);
  }
  filter_truncate(_filtered, 8);
  html_escape_append(out, _filtered);
  out.append("|");
  if constexpr (::injamm::detail::serializable_v<decltype(data.name)>) {
    _filtered.clear(); ::injamm::detail::serialize_value(_filtered, data.name);
  } else if constexpr (::injamm::detail::ct_glz_reflectable<decltype(data.name)>) {
    (void)::glz::write_json(data.name, _filtered);
  } else {
    _filtered.assign(data.name);
  }
  filter_left(_filtered, 10);
  html_escape_append(out, _filtered);
  out.append("|");
  if constexpr (::injamm::detail::is_chrono_time_point_v<decltype(data.total)>) {
    _filtered.clear(); ::injamm::detail::serialize_chrono(_filtered, data.total, ".2f");
  } else if constexpr (std::is_arithmetic_v<decltype(data.total)> && !std::is_same_v<std::remove_cvref_t<decltype(data.total)>, bool>) {
    _filtered.clear(); ::injamm::detail::serialize_formatted(_filtered, data.total, ".2f");
  } else if constexpr (std::is_same_v<std::remove_cvref_t<decltype(data.total)>, std::string> || std::is_same_v<std::remove_cvref_t<decltype(data.total)>, std::string_view>) {
    _filtered.clear(); ::injamm::detail::serialize_formatted(_filtered, ::injamm::detail::to_sv(data.total), ".2f");
  } else 
  if constexpr (::injamm::detail::serializable_v<decltype(data.total)>) {
    _filtered.clear(); ::injamm::detail::serialize_value(_filtered, data.total);
  } else if constexpr (::injamm::detail::ct_glz_reflectable<decltype(data.total)>) {
    (void)::glz::write_json(data.total, _filtered);
  } else {
    _filtered.assign(data.total);
  }
  filter_format(_filtered);
  html_escape_append(out, _filtered);
  out.append("|");
  if constexpr (::injamm::detail::serializable_v<decltype(data.name)>) {
    _filtered.clear(); ::injamm::detail::serialize_value(_filtered, data.name);
  } else if constexpr (::injamm::detail::ct_glz_reflectable<decltype(data.name)>) {
    (void)::glz::write_json(data.name, _filtered);
  } else {
    _filtered.assign(data.name);
  }
  filter_replace(_filtered, "i", "X");
  html_escape_append(out, _filtered);
  
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
render18(const T& data) {
  std::string out;
  auto result = render18(data, out);
  if (!result) return std::unexpected(result.error());
  return out;
}

} // namespace generated

#endif // RENDER_RENDER18_HPP
