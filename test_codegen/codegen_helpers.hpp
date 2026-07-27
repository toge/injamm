#pragma once
#ifndef INJAMM_CODEGEN_HELPERS_HPP
#define INJAMM_CODEGEN_HELPERS_HPP

#include <charconv>
#include <string>
#include <string_view>
#include <type_traits>

#ifndef INJAMM_CODEGEN_DISABLE_SIMD
#include <injamm/escape.hpp>
#if defined(__AVX2__)
#include <immintrin.h>
#elif defined(__SSE2__)
#include <emmintrin.h>
#elif defined(__ARM_NEON)
#include <arm_neon.h>
#endif
#endif

#ifndef INJAMM_CODEGEN_DISABLE_SIMD
inline void html_escape_append(std::string& out, std::string_view sv) {
  injamm::detail::html_escape_into(out, sv);
}
#else
inline void html_escape_append(std::string& out, std::string_view sv) {
  for (char c : sv) {
    switch (c) {
      case '&':  out += "&amp;";  break;
      case '<':  out += "&lt;";   break;
      case '>':  out += "&gt;";   break;
      case '"': out += "&quot;"; break;
      case '\'': out += "&#39;";  break;
      default:   out += c;       break;
    }
  }
}
#endif

template <typename N>
inline void append_number(std::string& out, N n) {
  char buf[64];
  auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), n);
  out.append(buf, ptr);
}

inline void filter_title(std::string& s) {
  bool prev_space = true;
  for (auto& c : s) {
    if (prev_space && c >= 'a' && c <= 'z') c -= 32;
    prev_space = (c == ' ');
  }
}

inline void filter_left(std::string& s, int n) {
  if (static_cast<int>(s.size()) < n)
    s = std::string(static_cast<std::size_t>(n) - s.size(), ' ') + s;
  else
    s = s.substr(0, static_cast<std::size_t>(n));
}

inline void filter_right(std::string& s, int n) {
  if (static_cast<int>(s.size()) < n)
    s.append(static_cast<std::size_t>(n) - s.size(), ' ');
  else
    s = s.substr(s.size() - static_cast<std::size_t>(n));
}

inline void filter_center(std::string& s, int n) {
  if (static_cast<int>(s.size()) < n) {
    auto total = static_cast<std::size_t>(n) - s.size();
    auto left = total / 2;
    auto right = total - left;
    s = std::string(left, ' ') + s + std::string(right, ' ');
  } else {
    s = s.substr(0, static_cast<std::size_t>(n));
  }
}

inline void filter_replace(std::string& s, std::string_view old_str, std::string_view new_str) {
  if (old_str.empty()) return;
  std::string result;
  std::size_t pos = 0;
  while (true) {
    auto found = s.find(old_str, pos);
    if (found == std::string::npos) break;
    result.append(s.data() + pos, found - pos);
    result += new_str;
    pos = found + old_str.size();
  }
  result.append(s.data() + pos, s.size() - pos);
  s = std::move(result);
}

inline void filter_default(std::string& s, std::string_view def) {
  if (s.empty()) s = def;
}

inline void filter_indent(std::string& s, int n) {
  if (n <= 0) return;
  std::string pad(static_cast<std::size_t>(n), ' ');
  std::string result;
  std::size_t pos = 0;
  while (true) {
    auto nl = s.find('\n', pos);
    if (nl == std::string::npos) break;
    result.append(s.data() + pos, nl - pos + 1);
    result += pad;
    pos = nl + 1;
  }
  result.append(s.data() + pos, s.size() - pos);
  s = std::move(result);
}

inline void filter_pad(std::string& s, int n, std::string_view pad_str) {
  if (n <= 0 || pad_str.empty()) return;
  while (static_cast<int>(s.size()) < n) {
    s += pad_str;
    if (static_cast<int>(s.size()) > n)
      s.resize(static_cast<std::size_t>(n));
  }
}

inline void filter_pluralize(std::string& s, std::string_view sg, std::string_view pl) {
  if (s == "1" || s == "1.0") {
    if (!sg.empty()) s = std::string(sg);
  } else {
    if (!pl.empty()) s = std::string(pl);
    else if (!sg.empty()) { s = std::string(sg); s += 's'; }
  }
}

inline void filter_format(std::string& s) {
  // formatは整形済み文字列をそのまま返す（runtimeではstd::vformat等を使用）
}

inline void filter_int_abs(std::string& s) {
  if (!s.empty() && s[0] == '-') s.erase(0, 1);
}

inline void filter_int_hex(std::string& s) {
  int val = 0;
  auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), val);
  if (ec == std::errc{}) {
    char buf[32];
    auto [p, e] = std::to_chars(buf, buf + sizeof(buf), val, 16);
    s.assign(buf, p);
  }
}

inline void filter_int_oct(std::string& s) {
  int val = 0;
  auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), val);
  if (ec == std::errc{}) {
    char buf[32];
    auto [p, e] = std::to_chars(buf, buf + sizeof(buf), val, 8);
    s.assign(buf, p);
  }
}

inline void filter_int_bin(std::string& s) {
  int val = 0;
  auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), val);
  if (ec == std::errc{}) {
    std::string result;
    if (val == 0) { result = "0"; }
    else {
      while (val > 0) { result = char('0' + (val & 1)) + result; val >>= 1; }
    }
    s = std::move(result);
  }
}

inline void filter_int_neg(std::string& s) {
  if (!s.empty() && s[0] == '-')
    s.erase(0, 1);
  else
    s = "-" + s;
}

inline void filter_int_mod(std::string& s, int n) {
  if (n == 0) return;
  int val = 0;
  auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), val);
  if (ec == std::errc{}) {
    val %= n;
    s = std::to_string(val);
  }
}

inline void filter_int_is_neg(std::string& s) {
  s = (!s.empty() && s[0] == '-') ? "true" : "false";
}

inline void filter_int_eq(std::string& s, int n) {
  int val = 0;
  auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), val);
  s = (ec == std::errc{} && val == n) ? "true" : "false";
}

inline void filter_int_ne(std::string& s, int n) {
  int val = 0;
  auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), val);
  s = (ec == std::errc{} && val != n) ? "true" : "false";
}

inline void filter_int_gt(std::string& s, int n) {
  int val = 0;
  auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), val);
  s = (ec == std::errc{} && val > n) ? "true" : "false";
}

inline void filter_int_gte(std::string& s, int n) {
  int val = 0;
  auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), val);
  s = (ec == std::errc{} && val >= n) ? "true" : "false";
}

inline void filter_int_lt(std::string& s, int n) {
  int val = 0;
  auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), val);
  s = (ec == std::errc{} && val < n) ? "true" : "false";
}

inline void filter_int_lte(std::string& s, int n) {
  int val = 0;
  auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), val);
  s = (ec == std::errc{} && val <= n) ? "true" : "false";
}

inline void filter_int_add(std::string& s, int n) {
  int val = 0;
  auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), val);
  if (ec == std::errc{}) s = std::to_string(val + n);
}

inline void filter_int_sub(std::string& s, int n) {
  int val = 0;
  auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), val);
  if (ec == std::errc{}) s = std::to_string(val - n);
}

inline void filter_int_mul(std::string& s, int n) {
  int val = 0;
  auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), val);
  if (ec == std::errc{}) s = std::to_string(val * n);
}

inline void filter_int_div(std::string& s, int n) {
  if (n == 0) return;
  int val = 0;
  auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), val);
  if (ec == std::errc{}) s = std::to_string(val / n);
}

inline void filter_float_precision(std::string& s, int n) {
  double val = 0.0;
  auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), val);
  if (ec == std::errc{}) {
    char buf[128];
    auto [p, e] = std::to_chars(buf, buf + sizeof(buf), val, std::chars_format::fixed, n);
    s.assign(buf, p);
  }
}

template <typename V>
inline void append_value(std::string& out, V const& v) {
  if constexpr (std::is_same_v<V, std::string> || std::is_same_v<V, std::string_view>) {
    out += v;
  } else if constexpr (std::is_same_v<V, const char*>) {
    out += v ? v : "";
  } else if constexpr (std::is_same_v<V, bool>) {
    out += v ? "true" : "false";
  } else if constexpr (std::is_arithmetic_v<V>) {
    append_number(out, v);
  } else {
    out += v;
  }
}

template <typename V>
inline void html_escape_append_value(std::string& out, V const& v) {
  if constexpr (std::is_same_v<V, std::string> || std::is_same_v<V, std::string_view>) {
    html_escape_append(out, v);
  } else if constexpr (std::is_same_v<V, const char*>) {
    html_escape_append(out, v ? v : "");
  } else if constexpr (std::is_same_v<V, bool>) {
    html_escape_append(out, v ? "true" : "false");
  } else if constexpr (std::is_arithmetic_v<V>) {
    std::string tmp;
    append_number(tmp, v);
    html_escape_append(out, tmp);
  } else if constexpr (std::is_enum_v<V>) {
    html_escape_append(out, std::to_string(static_cast<std::underlying_type_t<V>>(v)));
  } else if constexpr (std::is_arithmetic_v<V>) {
    std::string _ser = std::to_string(v);
    html_escape_append(out, _ser);
  } else {
    html_escape_append(out, v);
  }
}

#if defined(__AVX2__) && !defined(INJAMM_CODEGEN_DISABLE_SIMD)
inline void filter_to_upper(std::string& s) {
  auto* data = s.data();
  auto len = s.size();
  std::size_t i = 0;
  for (; i + 32 <= len; i += 32) {
    __m256i chunk = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(data + i));
    __m256i ge_a = _mm256_cmpeq_epi8(_mm256_max_epu8(chunk, _mm256_set1_epi8('a')), chunk);
    __m256i le_z = _mm256_cmpeq_epi8(_mm256_min_epu8(chunk, _mm256_set1_epi8('z')), chunk);
    __m256i is_lower = _mm256_and_si256(ge_a, le_z);
    __m256i result = _mm256_sub_epi8(chunk, _mm256_and_si256(is_lower, _mm256_set1_epi8(0x20)));
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(data + i), result);
  }
  for (; i < len; ++i)
    if (data[i] >= 'a' && data[i] <= 'z') data[i] -= 32;
}
#elif defined(__SSE2__) && !defined(INJAMM_CODEGEN_DISABLE_SIMD)
inline void filter_to_upper(std::string& s) {
  auto* data = s.data();
  auto len = s.size();
  std::size_t i = 0;
  for (; i + 16 <= len; i += 16) {
    __m128i chunk = _mm_loadu_si128(reinterpret_cast<const __m128i*>(data + i));
    __m128i ge_a = _mm_cmpeq_epi8(_mm_max_epu8(chunk, _mm_set1_epi8('a')), chunk);
    __m128i le_z = _mm_cmpeq_epi8(_mm_min_epu8(chunk, _mm_set1_epi8('z')), chunk);
    __m128i is_lower = _mm_and_si128(ge_a, le_z);
    __m128i result = _mm_sub_epi8(chunk, _mm_and_si128(is_lower, _mm_set1_epi8(0x20)));
    _mm_storeu_si128(reinterpret_cast<__m128i*>(data + i), result);
  }
  for (; i < len; ++i)
    if (data[i] >= 'a' && data[i] <= 'z') data[i] -= 32;
}
#elif defined(__ARM_NEON) && !defined(INJAMM_CODEGEN_DISABLE_SIMD)
inline void filter_to_upper(std::string& s) {
  auto* data = s.data();
  auto len = s.size();
  std::size_t i = 0;
  uint8x16_t a = vdupq_n_u8('a');
  uint8x16_t z = vdupq_n_u8('z');
  uint8x16_t offset = vdupq_n_u8(0x20);
  for (; i + 16 <= len; i += 16) {
    uint8x16_t chunk = vld1q_u8(reinterpret_cast<const uint8_t*>(data + i));
    uint8x16_t ge_a = vceqq_u8(vmaxq_u8(chunk, a), chunk);
    uint8x16_t le_z = vceqq_u8(vminq_u8(chunk, z), chunk);
    uint8x16_t is_lower = vandq_u8(ge_a, le_z);
    uint8x16_t result = vsubq_u8(chunk, vandq_u8(is_lower, offset));
    vst1q_u8(reinterpret_cast<uint8_t*>(data + i), result);
  }
  for (; i < len; ++i)
    if (data[i] >= 'a' && data[i] <= 'z') data[i] -= 32;
}
#else
inline void filter_to_upper(std::string& s) {
  for (auto& c : s)
    if (c >= 'a' && c <= 'z') c -= 32;
}
#endif

#if defined(__AVX2__) && !defined(INJAMM_CODEGEN_DISABLE_SIMD)
inline void filter_to_lower(std::string& s) {
  auto* data = s.data();
  auto len = s.size();
  std::size_t i = 0;
  for (; i + 32 <= len; i += 32) {
    __m256i chunk = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(data + i));
    __m256i ge_a = _mm256_cmpeq_epi8(_mm256_max_epu8(chunk, _mm256_set1_epi8('A')), chunk);
    __m256i le_z = _mm256_cmpeq_epi8(_mm256_min_epu8(chunk, _mm256_set1_epi8('Z')), chunk);
    __m256i is_upper = _mm256_and_si256(ge_a, le_z);
    __m256i result = _mm256_add_epi8(chunk, _mm256_and_si256(is_upper, _mm256_set1_epi8(0x20)));
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(data + i), result);
  }
  for (; i < len; ++i)
    if (data[i] >= 'A' && data[i] <= 'Z') data[i] += 32;
}
#elif defined(__SSE2__) && !defined(INJAMM_CODEGEN_DISABLE_SIMD)
inline void filter_to_lower(std::string& s) {
  auto* data = s.data();
  auto len = s.size();
  std::size_t i = 0;
  for (; i + 16 <= len; i += 16) {
    __m128i chunk = _mm_loadu_si128(reinterpret_cast<const __m128i*>(data + i));
    __m128i ge_a = _mm_cmpeq_epi8(_mm_max_epu8(chunk, _mm_set1_epi8('A')), chunk);
    __m128i le_z = _mm_cmpeq_epi8(_mm_min_epu8(chunk, _mm_set1_epi8('Z')), chunk);
    __m128i is_upper = _mm_and_si128(ge_a, le_z);
    __m128i result = _mm_add_epi8(chunk, _mm_and_si128(is_upper, _mm_set1_epi8(0x20)));
    _mm_storeu_si128(reinterpret_cast<__m128i*>(data + i), result);
  }
  for (; i < len; ++i)
    if (data[i] >= 'A' && data[i] <= 'Z') data[i] += 32;
}
#elif defined(__ARM_NEON) && !defined(INJAMM_CODEGEN_DISABLE_SIMD)
inline void filter_to_lower(std::string& s) {
  auto* data = s.data();
  auto len = s.size();
  std::size_t i = 0;
  uint8x16_t a = vdupq_n_u8('A');
  uint8x16_t z = vdupq_n_u8('Z');
  uint8x16_t offset = vdupq_n_u8(0x20);
  for (; i + 16 <= len; i += 16) {
    uint8x16_t chunk = vld1q_u8(reinterpret_cast<const uint8_t*>(data + i));
    uint8x16_t ge_a = vceqq_u8(vmaxq_u8(chunk, a), chunk);
    uint8x16_t le_z = vceqq_u8(vminq_u8(chunk, z), chunk);
    uint8x16_t is_upper = vandq_u8(ge_a, le_z);
    uint8x16_t result = vaddq_u8(chunk, vandq_u8(is_upper, offset));
    vst1q_u8(reinterpret_cast<uint8_t*>(data + i), result);
  }
  for (; i < len; ++i)
    if (data[i] >= 'A' && data[i] <= 'Z') data[i] += 32;
}
#else
inline void filter_to_lower(std::string& s) {
  for (auto& c : s)
    if (c >= 'A' && c <= 'Z') c += 32;
}
#endif

inline void filter_capitalize(std::string& s) {
  if (!s.empty()) s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
}

inline void filter_trim(std::string& s) {
  auto start = s.find_first_not_of(" \t");
  if (start == std::string::npos) {
    s.clear();
  } else {
    auto end = s.find_last_not_of(" \t");
    s.erase(end + 1);
    s.erase(0, start);
  }
}

inline void filter_ltrim(std::string& s) {
  auto start = s.find_first_not_of(" \t");
  if (start == std::string::npos) {
    s.clear();
  } else {
    s.erase(0, start);
  }
}

inline void filter_rtrim(std::string& s) {
  auto end = s.find_last_not_of(" \t");
  if (end == std::string::npos) {
    s.clear();
  } else {
    s.erase(end + 1);
  }
}

inline void filter_truncate(std::string& s, int max_len) {
  if (static_cast<int>(s.size()) > max_len) s.resize(static_cast<std::size_t>(max_len));
}

inline void filter_substr(std::string& s, int pos, int len) {
  if (pos < 0) pos = 0;
  if (pos >= static_cast<int>(s.size())) { s.clear(); return; }
  s = s.substr(static_cast<std::size_t>(pos), static_cast<std::size_t>(len));
}

inline void filter_numify(std::string& s) {
  if (s.size() <= 3) return;
  std::string result;
  int count = 0;
  for (int i = static_cast<int>(s.size()) - 1; i >= 0; --i) {
    if (count == 3) { result = ',' + result; count = 0; }
    result = s[static_cast<std::size_t>(i)] + result;
    ++count;
  }
  s = std::move(result);
}

inline void filter_zerofill(std::string& s, int width) {
  if (static_cast<int>(s.size()) < width)
    s = std::string(static_cast<std::size_t>(width) - s.size(), '0') + s;
}

inline void filter_repeat(std::string& s, int n) {
  if (n < 1) {
    s.clear();
  } else if (n > 1 && !s.empty()) {
    auto saved = s;
    s.reserve(saved.size() * static_cast<std::size_t>(n));
    for (int i = 1; i < n; ++i)
      s += saved;
  }
}

#endif // INJAMM_CODEGEN_HELPERS_HPP
