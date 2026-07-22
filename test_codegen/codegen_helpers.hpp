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

namespace generated {

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

} // namespace generated

#endif // INJAMM_CODEGEN_HELPERS_HPP
