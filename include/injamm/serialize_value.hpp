#pragma once

#include "enum_io.hpp"
#include <array>
#include <charconv>
#include <chrono>
#include <concepts>
#include <ctime>
#ifdef INJAMM_USE_FMT
#include <fmt/format.h>
#else
#include <format>
#endif
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

/** @brief std::chrono::time_point かどうかを判定する型特性 */
template <class T>
struct is_chrono_time_point : std::false_type {};

template <class Clock, class Duration>
struct is_chrono_time_point<std::chrono::time_point<Clock, Duration>> : std::true_type {};

template <class T>
inline constexpr bool is_chrono_time_point_v = is_chrono_time_point<T>::value;

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

/** @brief chrono time_point を指定フォーマットでバッファに追記する
 *
 *  fmt には strftime スタイルの指定子を渡す（例: "%Y-%m-%d"）。
 *  fmt 未指定時は ISO 8601 形式（%Y-%m-%dT%H:%M:%S）で出力する。
 *  strftime は null-terminated 文字列を要求するため、内部で std::string に変換する。
 *
 *  @tparam Buffer 出力バッファ型
 *  @tparam Clock 時計型
 *  @tparam Duration 時間間隔型
 *  @param[in,out] out 出力先バッファ
 *  @param[in] tp 変換する time_point
 *  @param[in] fmt strftime フォーマット文字列（デフォルト: ISO 8601）
 */
template <class Buffer, class Clock, class Duration>
inline void serialize_chrono(Buffer& out, std::chrono::time_point<Clock, Duration> const& tp,
                             std::string_view fmt = "%Y-%m-%dT%H:%M:%S") {
  std::chrono::system_clock::time_point sys_tp;
  if constexpr (std::is_same_v<Clock, std::chrono::system_clock>) {
    sys_tp = tp;
  } else {
    // clock_cast は macOS libc++ 旧版や emscripten 等で未実装なため直接使用しない。
    // system_clock 以外のクロックはエポックを共有するとみなして duration をキャストする。
    sys_tp = std::chrono::system_clock::time_point{
        std::chrono::duration_cast<std::chrono::system_clock::duration>(tp.time_since_epoch())};
  }
  auto tt = std::chrono::system_clock::to_time_t(sys_tp);
  std::tm tm{};
#if defined(_WIN32)
  localtime_s(&tm, &tt);
#else
  localtime_r(&tt, &tm);
#endif
  char buf[256];
  std::string fmt_null{fmt};
  auto len = std::strftime(buf, sizeof(buf), fmt_null.c_str(), &tm);
  if (len > 0) out.append(std::string_view{buf, static_cast<std::size_t>(len)});
}

/** @brief 算術型を std::format スタイルのフォーマット指定子でバッファに追記する
 *
 *  fmt には std::format のフォーマットスペックを指定する（引数の部分のみ）。
 *  例: "05" → "{:05}" → std::format("{:05}", 42) → "00042"
 *  例: ".2f" → "{:.2f}" → std::format("{:.2f}", 3.14) → "3.14"
 *
 *  @tparam Buffer 出力バッファ型
 *  @tparam T 算術型
 *  @param[in,out] out 出力先バッファ
 *  @param[in] value 変換する値
 *  @param[in] fmt std::format フォーマットスペック
 */
template <class Buffer, class T>
  requires std::is_arithmetic_v<T> && (!std::same_as<T, bool>)
inline void serialize_formatted(Buffer& out, T value, std::string_view fmt) {
#ifdef INJAMM_USE_FMT
  std::string fmt_str = "{:";
  fmt_str.append(fmt);
  fmt_str.push_back('}');
  auto result = fmt::vformat(fmt_str, fmt::make_format_args(value));
  out.append(result);
#else
  // libc++ (macOS) では "{:05}" の 0-埋めフラグが誤って解析され std::format_error となる。
  // 純粋な 0埋め幅指定（例: "05","008"）は自前で実装し、其他は std::vformat に委譲する。
  bool zerofill_only = std::is_integral_v<T> && !fmt.empty() && fmt.front() == '0';
  if (zerofill_only) {
    for (char c : fmt)
      if (c < '0' || c > '9') { zerofill_only = false; break; }
  }
  if (zerofill_only) {
    int width = 0;
    for (char c : fmt) width = width * 10 + (c - '0');
    std::string s = std::to_string(value);
    bool const  neg = !s.empty() && s[0] == '-';
    std::string digits = neg ? s.substr(1) : s;
    if (static_cast<int>(digits.size()) < width) {
      s = std::string(static_cast<std::size_t>(width) - digits.size(), '0') + digits;
    }
    if (neg) s = "-" + s;
    out.append(s);
    return;
  }
  std::string fmt_str = "{:";
  fmt_str.append(fmt);
  fmt_str.push_back('}');
  auto result = std::vformat(fmt_str, std::make_format_args(value));
  out.append(result);
#endif
}

/** @brief 文字列を std::format スタイルのフォーマット指定子でバッファに追記する
 *
 *  fmt には std::format のフォーマットスペックを指定する（引数の部分のみ）。
 *  例: "<20" → "{:<20}" → 左寄せ幅20
 *  例: ">20" → "{:>20}" → 右寄せ幅20
 *  例: "*^20" → "{:*^20}" → 中央寄せ '*' 埋め幅20
 *
 *  @tparam Buffer 出力バッファ型
 *  @param[in,out] out 出力先バッファ
 *  @param[in] value 変換する文字列
 *  @param[in] fmt std::format フォーマットスペック
 */
template <class Buffer>
inline void serialize_formatted(Buffer& out, std::string_view value, std::string_view fmt) {
  std::string fmt_str = "{:";
  fmt_str.append(fmt);
  fmt_str.push_back('}');
#ifdef INJAMM_USE_FMT
  auto result = fmt::vformat(fmt_str, fmt::make_format_args(value));
#else
  auto result = std::vformat(fmt_str, std::make_format_args(value));
#endif
  out.append(result);
}

} // namespace injamm::detail
