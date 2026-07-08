#pragma once

#include "enum_io.hpp"
#include <array>
#include <charconv>
#include <concepts>
#include <glaze/util/zmij.hpp>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>

namespace injamm::detail {

/** @brief シリアライズ可能な型の判定
 *
 *  整数型（bool除く）、浮動小数点型、std::string、std::string_view、enum 型をシリアライズ対象とする。
 *  std::vector<U> などのコンテナ型を serialize_value から除外するガードとして機能する。
 */
template <class T>
inline constexpr bool serializable_v =
    std::integral<T> || std::floating_point<T> ||
    std::same_as<T, std::string> || std::same_as<T, std::string_view> ||
    std::is_enum_v<T>;

/** @brief std::optional かどうかを判定する型特性 */
template <class T>
inline constexpr bool is_std_optional_v = false;

template <class T>
inline constexpr bool is_std_optional_v<std::optional<T>> = true;

/** @brief std::map / std::unordered_map かどうかを判定する型特性 */
template <class T>
inline constexpr bool is_std_map_like_v = false;

template <class K, class V, class Comp, class Alloc>
inline constexpr bool is_std_map_like_v<std::map<K, V, Comp, Alloc>> = true;

template <class K, class V, class Hash, class Eq, class Alloc>
inline constexpr bool is_std_map_like_v<std::unordered_map<K, V, Hash, Eq, Alloc>> = true;

/** @brief 整数型（bool除く）をバッファに変換して追記する
 *
 *  @tparam Buffer 出力バッファ型
 *  @tparam T 整数型
 *  @param[in,out] out 出力先バッファ
 *  @param[in] value 変換する値
 */
template <class Buffer, class T>
  requires std::integral<T> && (!std::same_as<T, bool>)
inline void serialize_value(Buffer& out, T value) {
  std::array<char, 32> buf{};
  auto [ptr, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), value);
  if (ec == std::errc{}) {
    out.append(std::string_view{buf.data(), static_cast<std::size_t>(ptr - buf.data())});
  }
}

/** @brief bool型をバッファに変換して追記する
 *
 *  @tparam Buffer 出力バッファ型
 *  @param[in,out] out 出力先バッファ
 *  @param[in] b 変換する値
 */
template <class Buffer>
inline void serialize_value(Buffer& out, bool b) {
  out.append(b ? std::string_view{"true"} : std::string_view{"false"});
}

/** @brief string_view型をバッファに追記する
 *
 *  @tparam Buffer 出力バッファ型
 *  @param[in,out] out 出力先バッファ
 *  @param[in] s 追記する文字列
 */
template <class Buffer>
inline void serialize_value(Buffer& out, std::string_view s) {
  out.append(s);
}

/** @brief string型をバッファに追記する
 *
 *  @tparam Buffer 出力バッファ型
 *  @param[in,out] out 出力先バッファ
 *  @param[in] s 追記する文字列
 */
template <class Buffer>
inline void serialize_value(Buffer& out, std::string const& s) {
  out.append(std::string_view{s});
}

/** @brief std::optional 型をバッファに変換して追記する
 *
 *  値を持つ場合は内部値をシリアライズし、空の場合は何も出力しない。
 *
 *  @tparam Buffer 出力バッファ型
 *  @tparam T optional の内部型
 *  @param[in,out] out 出力先バッファ
 *  @param[in] opt optional 値
 */
template <class Buffer, class T>
inline void serialize_value(Buffer& out, std::optional<T> const& opt) {
  if (opt.has_value()) {
    serialize_value(out, *opt);
  }
}

/** @brief 浮動小数点型をバッファに変換して追記する
 *
 *  glaze 7.5.0 で統合された zmij による高速 float/double 文字列化。
 *
 *  @tparam Buffer 出力バッファ型
 *  @tparam T 浮動小数点型
 *  @param[in,out] out 出力先バッファ
 *  @param[in] value 変換する値
 */
template <class Buffer, class T>
  requires std::floating_point<T>
inline void serialize_value(Buffer& out, T value) {
  std::array<char, glz::zmij::double_buffer_size> buf{};
  auto end = glz::to_chars(buf.data(), value);
  out.append(std::string_view{buf.data(), static_cast<std::size_t>(end - buf.data())});
}

/** @brief enum 型をバッファに変換して追記する（filter scratch 用、常に raw 出力）
 *
 *  フィルタパスは独自エスケープ処理を持つため、ここでは raw=true で serialize_enum を呼ぶ。
 *
 *  @tparam Buffer 出力バッファ型
 *  @tparam E enum 型
 *  @param[in,out] out 出力先バッファ
 *  @param[in] value 変換する enum 値
 */
template <class Buffer, class E>
  requires std::is_enum_v<E>
inline void serialize_value(Buffer& out, E value) {
  serialize_enum(out, value, /*raw=*/true);
}

} // namespace injamm::detail
