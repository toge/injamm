#pragma once

/**
 * @file enum_io.hpp
 * @brief enum型のシリアライズヘルパ
 *
 * enchantum を用いて enum 値を列挙子名文字列に変換し、バッファへ追記する。
 * 未知値（enchantum が認識できない値）は underlying 整数として10進出力する。
 */

#include "escape.hpp"
#include <array>
#include <charconv>
#ifndef INJAMM_NO_ENUM_REGISTRY
#include <enchantum/enchantum.hpp>
#endif
#include <string_view>
#include <type_traits>

namespace injamm::detail {

/**
 * @brief enum 値をバッファに文字列として追記する
 *
 * enchantum::to_string で列挙子名を取得し、rawフラグに応じてHTMLエスケープを行う。
 * 未知値の場合は underlying 型の整数を10進文字列として追記する（フォールバック）。
 *
 * @tparam Buffer 出力バッファ型（std::string など）
 * @tparam E enum 型
 * @param[in,out] out 出力先バッファ
 * @param[in] value シリアライズする enum 値
 * @param[in] raw true のとき HTMLエスケープを行わない（{{{...}}} 相当）
 */
template <class Buffer, class E>
  requires std::is_enum_v<E>
inline void serialize_enum(Buffer& out, E value, bool raw) {
#ifndef INJAMM_NO_ENUM_REGISTRY
  auto const name = enchantum::to_string(value);
  if (!name.empty()) {
    /** 有効な列挙子名が取得できた場合: rawフラグに応じてエスケープ */
    if (raw) {
      out.append(name.data(), name.size());
    } else {
      html_escape_into(out, name);
    }
    return;
  }
#endif
  /** フォールバック: underlying 整数を10進で出力 */
  using U = std::underlying_type_t<E>;
  std::array<char, 32> buf{};
  auto [ptr, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), static_cast<U>(value));
  if (ec == std::errc{}) {
    out.append(std::string_view{buf.data(), static_cast<std::size_t>(ptr - buf.data())});
  }
}

} // namespace injamm::detail
