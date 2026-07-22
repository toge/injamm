#pragma once
/**
 * @file render.hpp
 * @brief injamm_codegen によって自動生成されたレンダリング関数
 */

#include <charconv>
#include <cctype>
#include <cmath>
#include <expected>
#include <string>
#include <string_view>

#include <injamm/types.hpp>
#include <injamm/escape.hpp>
#if defined(__AVX2__)
#include <immintrin.h>
#elif defined(__SSE2__)
#include <emmintrin.h>
#endif

namespace generated {

/** @brief HTML エスケープ関数（SIMD 対応） */
inline void html_escape_append(std::string& out, std::string_view sv) {
  injamm::detail::html_escape_into(out, sv);
}

/** @brief 整数→文字列変換ヘルパ */
template <typename N>
inline void append_number(std::string& out, N n) {
  char buf[64];
  auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), n);
  out.append(buf, ptr);
}

/** @brief 値を文字列に変換して追加（数値/bool/文字列に対応） */
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

/** @brief 値を HTML エスケープして追加 */
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

/** @brief 文字列の大文字変換（SIMD 対応） */
#if defined(__AVX2__)
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
#elif defined(__SSE2__)
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
#else
inline void filter_to_upper(std::string& s) {
  for (auto& c : s)
    if (c >= 'a' && c <= 'z') c -= 32;
}
#endif

/** @brief 文字列の小文字変換（SIMD 対応） */
#if defined(__AVX2__)
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
#elif defined(__SSE2__)
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
#else
inline void filter_to_lower(std::string& s) {
  for (auto& c : s)
    if (c >= 'A' && c <= 'Z') c += 32;
}
#endif

/** @brief 文字列の先頭大文字変換 */
inline void filter_capitalize(std::string& s) {
  if (!s.empty()) s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
}

/** @brief 文字列のトリム */
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

/** @brief 文字列の左トリム */
inline void filter_ltrim(std::string& s) {
  auto start = s.find_first_not_of(" \t");
  if (start == std::string::npos) {
    s.clear();
  } else {
    s.erase(0, start);
  }
}

/** @brief 文字列の右トリム */
inline void filter_rtrim(std::string& s) {
  auto end = s.find_last_not_of(" \t");
  if (end == std::string::npos) {
    s.clear();
  } else {
    s.erase(end + 1);
  }
}

/** @brief 文字列の切り詰め */
inline void filter_truncate(std::string& s, int max_len) {
  if (static_cast<int>(s.size()) > max_len) s.resize(static_cast<std::size_t>(max_len));
}

/** @brief 部分文字列 */
inline void filter_substr(std::string& s, int pos, int len) {
  if (pos < 0) pos = 0;
  if (pos >= static_cast<int>(s.size())) { s.clear(); return; }
  s = s.substr(static_cast<std::size_t>(pos), static_cast<std::size_t>(len));
}

/** @brief 整数のカンマ区切り */
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

/** @brief 整数の0埋め */
inline void filter_zerofill(std::string& s, int width) {
  if (static_cast<int>(s.size()) < width)
    s = std::string(static_cast<std::size_t>(width) - s.size(), '0') + s;
}


/** @brief テンプレート文字列から生成されたレンダリング関数 */
template <typename T>
[[nodiscard]] std::expected<std::string, injamm::error_ctx>
render(const T& data) {
  std::string out;
  out.reserve(256);
  
  out += "Items:\n";
  for (std::size_t _i1 = 0; _i1 < data.items.size(); ++_i1) {
    const auto& _item1 = data.items[_i1];
    out += "\n- ";
    html_escape_append_value(out, _item1.name);
    out += " x";
    html_escape_append_value(out, _item1.quantity);
    out += "\n";
  }
  
  return out;
}

} // namespace generated
