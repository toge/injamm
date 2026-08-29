#pragma once
#ifndef INJAMM_CODEGEN_HELPERS_HPP
#define INJAMM_CODEGEN_HELPERS_HPP

#include <charconv>
#include <cmath>
#include <injamm/glz_dispatch.hpp>
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
template <class Buffer>
inline void html_escape_append(Buffer& out, std::string_view sv) {
  injamm::detail::html_escape_into(out, sv);
}
#else
template <class Buffer>
inline void html_escape_append(Buffer& out, std::string_view sv) {
  for (char c : sv) {
    switch (c) {
      case '&':  out.append("&amp;");  break;
      case '<':  out.append("&lt;");   break;
      case '>':  out.append("&gt;");   break;
      case '"': out.append("&quot;"); break;
      case '\'': out.append("&#39;");  break;
      default:   out.append(1, c);    break;
    }
  }
}
#endif

template <class Buffer, typename N>
inline void append_number(Buffer& out, N n) {
  char buf[64];
  auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), n);
  out.append(buf, static_cast<std::size_t>(ptr - buf));
}

inline void filter_title(std::string& s) {
  bool new_word = true;
  for (auto& c : s) {
    if (c == ' ' || c == '\t') {
      new_word = true;
    } else if (new_word && c >= 'a' && c <= 'z') {
      c -= 32;
      new_word = false;
    } else {
      new_word = false;
    }
  }
}

inline void filter_left(std::string& s, int n) {
  if (static_cast<int>(s.size()) < n)
    s.insert(0, static_cast<std::size_t>(n) - s.size(), ' ');
}

inline void filter_right(std::string& s, int n) {
  if (static_cast<int>(s.size()) < n)
    s.append(static_cast<std::size_t>(n) - s.size(), ' ');
}

inline void filter_center(std::string& s, int n) {
  if (static_cast<int>(s.size()) < n) {
    auto pad = static_cast<std::size_t>(n) - s.size();
    auto left = pad / 2;
    s.reserve(static_cast<std::size_t>(n));
    s.insert(0, left, ' ');
    s.append(pad - left, ' ');
  }
}

inline void filter_replace(std::string& s, std::string_view old_str, std::string_view new_str) {
  if (old_str.empty()) {
    // VM (filters.hpp) と同一: 引数なし replace は全 '\n' を ' ' に置換
    for (auto& c : s) {
      if (c == '\n') c = ' ';
    }
    return;
  }
  std::string result;
  std::size_t pos = 0;
  while (pos < s.size()) {
    auto found = s.find(old_str, pos);
    if (found == std::string::npos) {
      result.append(s, pos, std::string::npos);
      break;
    }
    result.append(s, pos, found - pos);
    result.append(new_str);
    pos = found + old_str.size();
  }
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
  if (n <= 0) return;
  if (static_cast<int>(s.size()) < n) {
    std::string_view use_pad = pad_str.empty() ? std::string_view{" "} : pad_str;
    while (static_cast<int>(s.size()) < n) {
      s.append(use_pad);
    }
    s.resize(static_cast<std::size_t>(n));
  }
}

inline void filter_pluralize(std::string& s, std::string_view sg, std::string_view pl) {
  long long val = 0;
  if (auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), val); ec == std::errc{}) {
    s.assign((val == 1) ? sg : pl);
  }
}

inline void filter_format(std::string& s) {
  // formatは整形済み文字列をそのまま返す（runtimeではstd::vformat等を使用）
}

inline void filter_int_abs(std::string& s) {
  // VM (filters.hpp) と同一: 小数/指数表記は double で処理
  if (s.find('.') != std::string::npos || s.find('e') != std::string::npos || s.find('E') != std::string::npos) {
    double val = 0.0;
    if (auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), val); ec == std::errc{}) {
      char buf[128];
      auto [p2, ec2] = std::to_chars(buf, buf + sizeof(buf), std::abs(val));
      if (ec2 == std::errc{}) {
        s.assign(buf, p2);
      }
    }
  } else {
    long long val = 0;
    if (auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), val); ec == std::errc{}) {
      char buf[32];
      auto [tp, tec] = std::to_chars(buf, buf + sizeof(buf), val);
      if (tec == std::errc{}) {
        std::string_view sv(buf, static_cast<std::size_t>(tp - buf));
        if (!sv.empty() && sv[0] == '-') {
          sv.remove_prefix(1);
        }
        s.assign(sv);
      }
    }
  }
}

inline void filter_int_hex(std::string& s) {
  long long val = 0;
  if (auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), val); ec == std::errc{}) {
    char buf[32];
    auto [p, e] = std::to_chars(buf, buf + sizeof(buf), val, 16);
    s.assign(buf, p);
  }
}

inline void filter_int_oct(std::string& s) {
  long long val = 0;
  if (auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), val); ec == std::errc{}) {
    char buf[32];
    auto [p, e] = std::to_chars(buf, buf + sizeof(buf), val, 8);
    s.assign(buf, p);
  }
}

inline void filter_int_bin(std::string& s) {
  long long val = 0;
  if (auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), val); ec == std::errc{}) {
    char buf[64];
    auto [p, ec2] = std::to_chars(buf, buf + sizeof(buf), val, 2);
    if (ec2 == std::errc{}) {
      s.assign(buf, p);
    }
  }
}

inline void filter_int_neg(std::string& s) {
  // VM (filters.hpp) と同一: 小数/指数表記は double で処理
  if (s.find('.') != std::string::npos || s.find('e') != std::string::npos || s.find('E') != std::string::npos) {
    double val = 0.0;
    if (auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), val); ec == std::errc{}) {
      char buf[128];
      auto [p2, ec2] = std::to_chars(buf, buf + sizeof(buf), -val);
      if (ec2 == std::errc{}) {
        s.assign(buf, p2);
      }
    }
  } else {
    long long val = 0;
    if (auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), val); ec == std::errc{}) {
      char buf[32];
      auto [tp, tec] = std::to_chars(buf, buf + sizeof(buf), val);
      if (tec == std::errc{}) {
        std::string_view sv(buf, static_cast<std::size_t>(tp - buf));
        if (!sv.empty() && sv[0] == '-') {
          sv.remove_prefix(1);
          s.assign(sv);
        } else {
          s = std::string("-") + std::string(sv);
        }
      }
    }
  }
}

inline void filter_int_mod(std::string& s, int n) {
  if (n == 0) return;
  long long val = 0;
  if (auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), val); ec == std::errc{}) {
    val %= n;
    s = std::to_string(val);
  }
}

inline void filter_int_is_neg(std::string& s) {
  // VM (filters.hpp) と同一: 数値パースしてから判定
  if (s.find('.') != std::string::npos || s.find('e') != std::string::npos || s.find('E') != std::string::npos) {
    double val = 0.0;
    if (auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), val); ec == std::errc{}) {
      s = val < 0 ? "true" : "false";
      return;
    }
  }
  long long val = 0;
  if (auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), val); ec == std::errc{}) {
    s = val < 0 ? "true" : "false";
  } else {
    s = "false";
  }
}

/** @brief 小数文字列を含む場合の比較ヘルパ (VM の ne/gt/gte/lt/lte と同一ロジック) */
inline bool filter_int_cmp_float(std::string_view s, int op, int target) {
  // op: 0=ne, 1=gt, 2=gte, 3=lt, 4=lte（比較オペコード）
  double val = 0.0;
  if (auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), val); ec == std::errc{}) {
    double ftarget = static_cast<double>(target);
    switch (op) {
      case 0: return val != ftarget;
      case 1: return val > ftarget;
      case 2: return val >= ftarget;
      case 3: return val < ftarget;
      case 4: return val <= ftarget;
      default: return false;
    }
  }
  return false;
}

inline void filter_int_eq(std::string& s, int n) {
  long long val = 0;
  if (auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), val); ec == std::errc{}) {
    s = (val == n) ? "true" : "false";
  } else {
    s = "false";
  }
}

inline void filter_int_ne(std::string& s, int n) {
  if (s.find('.') != std::string::npos || s.find('e') != std::string::npos || s.find('E') != std::string::npos) {
    s = filter_int_cmp_float(s, 0, n) ? "true" : "false";
    return;
  }
  long long val = 0;
  if (auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), val); ec == std::errc{}) {
    s = (val != n) ? "true" : "false";
  } else {
    s = "false";
  }
}

inline void filter_int_gt(std::string& s, int n) {
  if (s.find('.') != std::string::npos || s.find('e') != std::string::npos || s.find('E') != std::string::npos) {
    s = filter_int_cmp_float(s, 1, n) ? "true" : "false";
    return;
  }
  long long val = 0;
  if (auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), val); ec == std::errc{}) {
    s = (val > n) ? "true" : "false";
  } else {
    s = "false";
  }
}

inline void filter_int_gte(std::string& s, int n) {
  if (s.find('.') != std::string::npos || s.find('e') != std::string::npos || s.find('E') != std::string::npos) {
    s = filter_int_cmp_float(s, 2, n) ? "true" : "false";
    return;
  }
  long long val = 0;
  if (auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), val); ec == std::errc{}) {
    s = (val >= n) ? "true" : "false";
  } else {
    s = "false";
  }
}

inline void filter_int_lt(std::string& s, int n) {
  if (s.find('.') != std::string::npos || s.find('e') != std::string::npos || s.find('E') != std::string::npos) {
    s = filter_int_cmp_float(s, 3, n) ? "true" : "false";
    return;
  }
  long long val = 0;
  if (auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), val); ec == std::errc{}) {
    s = (val < n) ? "true" : "false";
  } else {
    s = "false";
  }
}

inline void filter_int_lte(std::string& s, int n) {
  if (s.find('.') != std::string::npos || s.find('e') != std::string::npos || s.find('E') != std::string::npos) {
    s = filter_int_cmp_float(s, 4, n) ? "true" : "false";
    return;
  }
  long long val = 0;
  if (auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), val); ec == std::errc{}) {
    s = (val <= n) ? "true" : "false";
  } else {
    s = "false";
  }
}

inline void filter_int_add(std::string& s, int n) {
  // VM (filters.hpp) と同一: 小数/指数表記は double で処理
  if (s.find('.') != std::string::npos || s.find('e') != std::string::npos || s.find('E') != std::string::npos) {
    double val = 0.0;
    if (auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), val); ec == std::errc{}) {
      char buf[128];
      auto [p2, ec2] = std::to_chars(buf, buf + sizeof(buf), val + n);
      if (ec2 == std::errc{}) {
        s.assign(buf, p2);
      }
    }
  } else {
    long long val = 0;
    if (auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), val); ec == std::errc{}) {
      char buf[32];
      if (auto [tp, tec] = std::to_chars(buf, buf + sizeof(buf), val + n); tec == std::errc{}) {
        s.assign(buf, tp - buf);
      }
    }
  }
}

inline void filter_int_sub(std::string& s, int n) {
  if (s.find('.') != std::string::npos || s.find('e') != std::string::npos || s.find('E') != std::string::npos) {
    double val = 0.0;
    if (auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), val); ec == std::errc{}) {
      char buf[128];
      auto [p2, ec2] = std::to_chars(buf, buf + sizeof(buf), val - n);
      if (ec2 == std::errc{}) {
        s.assign(buf, p2);
      }
    }
  } else {
    long long val = 0;
    if (auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), val); ec == std::errc{}) {
      char buf[32];
      if (auto [tp, tec] = std::to_chars(buf, buf + sizeof(buf), val - n); tec == std::errc{}) {
        s.assign(buf, tp - buf);
      }
    }
  }
}

inline void filter_int_mul(std::string& s, int n) {
  if (s.find('.') != std::string::npos || s.find('e') != std::string::npos || s.find('E') != std::string::npos) {
    double val = 0.0;
    if (auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), val); ec == std::errc{}) {
      char buf[128];
      auto [p2, ec2] = std::to_chars(buf, buf + sizeof(buf), val * n);
      if (ec2 == std::errc{}) {
        s.assign(buf, p2);
      }
    }
  } else {
    long long val = 0;
    if (auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), val); ec == std::errc{}) {
      char buf[32];
      if (auto [tp, tec] = std::to_chars(buf, buf + sizeof(buf), val * n); tec == std::errc{}) {
        s.assign(buf, tp - buf);
      }
    }
  }
}

inline void filter_int_div(std::string& s, int n) {
  if (n == 0) return;
  if (s.find('.') != std::string::npos || s.find('e') != std::string::npos || s.find('E') != std::string::npos) {
    double val = 0.0;
    if (auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), val); ec == std::errc{}) {
      char buf[128];
      auto [p2, ec2] = std::to_chars(buf, buf + sizeof(buf), val / n);
      if (ec2 == std::errc{}) {
        s.assign(buf, p2);
      }
    }
  } else {
    long long val = 0;
    if (auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), val); ec == std::errc{}) {
      char buf[32];
      if (auto [tp, tec] = std::to_chars(buf, buf + sizeof(buf), val / n); tec == std::errc{}) {
        s.assign(buf, tp - buf);
      }
    }
  }
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

// round: VM (filters.hpp) と同一の half-away-from-zero 丸め
// precision < 0 → std::round で整数丸め（小数部除去）
// precision >= 0 → pow10 倍して std::round → pow10 で割って固定小数点表示
inline void filter_float_round(std::string& s, int precision) {
  double val = 0.0;
  auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), val);
  if (ec != std::errc{}) return;
  if (precision < 0) {
    val = std::round(val);
    char buf[128];
    auto [p, e] = std::to_chars(buf, buf + sizeof(buf), val);
    if (e == std::errc{}) s.assign(buf, p);
    return;
  }
  double pow10 = 1.0;
  for (int i = 0; i < precision; ++i) pow10 *= 10.0;
  val = std::round(val * pow10) / pow10;
  char buf[128];
  auto [p, e] = std::to_chars(buf, buf + sizeof(buf), val, std::chars_format::fixed, precision);
  if (e == std::errc{}) s.assign(buf, p);
}

template <class Buffer, typename V>
inline void append_value(Buffer& out, V const& v) {
  if constexpr (std::is_same_v<V, std::string> || std::is_same_v<V, std::string_view>) {
    out.append(v);
  } else if constexpr (std::is_same_v<V, const char*>) {
    out.append(v ? v : "");
  } else if constexpr (std::is_same_v<V, bool>) {
    out.append(v ? "true" : "false");
  } else if constexpr (std::is_arithmetic_v<V>) {
    append_number(out, v);
  } else if constexpr (std::is_enum_v<V>) {
    out.append(std::to_string(static_cast<std::underlying_type_t<V>>(v)));
  } else if constexpr (injamm::detail::serializable_v<V>) {
    serialize_value(out, v);
  } else if constexpr (injamm::detail::ct_glz_reflectable<V>) {
    std::string scratch;
    (void)::glz::write_json(v, scratch);
    out.append(scratch);
  } else {
    out.append(v);
  }
}

template <typename V>
constexpr bool value_empty(V const& v) {
  if constexpr (std::is_same_v<V, const char*>) {
    return !v || v[0] == '\0';
  } else if constexpr (std::is_same_v<V, std::string> || std::is_same_v<V, std::string_view>) {
    return v.empty();
  } else {
    return v.empty();
  }
}

template <typename V>
constexpr std::size_t value_size(V const& v) {
  if constexpr (std::is_same_v<V, const char*>) {
    return v ? std::string_view{v}.size() : 0;
  } else {
    return v.size();
  }
}

template <class Buffer, typename V>
inline void html_escape_append_value(Buffer& out, V const& v) {
  if constexpr (std::is_same_v<V, std::string> || std::is_same_v<V, std::string_view>) {
    html_escape_append(out, v);
  } else if constexpr (std::is_same_v<V, const char*>) {
    html_escape_append(out, v ? v : "");
  } else if constexpr (std::is_same_v<V, bool>) {
    out.append(v ? "true" : "false");
  } else if constexpr (std::is_arithmetic_v<V>) {
    append_number(out, v);
  } else if constexpr (std::is_enum_v<V>) {
    html_escape_append(out, std::to_string(static_cast<std::underlying_type_t<V>>(v)));
  } else if constexpr (injamm::detail::serializable_v<V>) {
    std::string scratch;
    serialize_value(scratch, v);
    html_escape_append(out, scratch);
  } else if constexpr (injamm::detail::ct_glz_reflectable<V>) {
    std::string scratch;
    (void)::glz::write_json(v, scratch);
    html_escape_append(out, scratch);
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

inline void filter_trim(std::string& s, std::string const& chars = "") {
  if (chars.empty()) {
    auto start = s.find_first_not_of(" \t\n\r\v\f");
    if (start == std::string::npos) { s.clear(); }
    else { auto end = s.find_last_not_of(" \t\n\r\v\f"); s.erase(end + 1); s.erase(0, start); }
  } else {
    auto start = s.find_first_not_of(chars);
    if (start == std::string::npos) { s.clear(); }
    else { auto end = s.find_last_not_of(chars); s.erase(end + 1); s.erase(0, start); }
  }
}

inline void filter_ltrim(std::string& s, std::string const& chars = "") {
  if (chars.empty()) {
    auto start = s.find_first_not_of(" \t\n\r\v\f");
    if (start == std::string::npos) { s.clear(); }
    else { s.erase(0, start); }
  } else {
    auto start = s.find_first_not_of(chars);
    if (start == std::string::npos) { s.clear(); }
    else { s.erase(0, start); }
  }
}

inline void filter_rtrim(std::string& s, std::string const& chars = "") {
  if (chars.empty()) {
    auto end = s.find_last_not_of(" \t\n\r\v\f");
    if (end == std::string::npos) { s.clear(); }
    else { s.erase(end + 1); }
  } else {
    auto end = s.find_last_not_of(chars);
    if (end == std::string::npos) { s.clear(); }
    else { s.erase(end + 1); }
  }
}

inline void filter_truncate(std::string& s, int max_len) {
  if (static_cast<int>(s.size()) > max_len && max_len >= 3) {
    s.erase(static_cast<std::size_t>(max_len) - 3);
    s.append("...");
  } else if (static_cast<int>(s.size()) > max_len) {
    s.erase(static_cast<std::size_t>(max_len));
  }
}

inline void filter_substr(std::string& s, int pos, int len) {
  // VM (filters.hpp) と同一: 負の start → clear、len<=0 → 末尾残す
  if (pos < 0) { s.clear(); return; }
  if (pos >= static_cast<int>(s.size())) { s.clear(); return; }
  if (len > 0 && pos + len < static_cast<int>(s.size())) {
    s.erase(static_cast<std::size_t>(pos + len));
  }
  if (pos > 0) {
    s.erase(0, static_cast<std::size_t>(pos));
  }
}

/** @brief 数字列を右から 3 桁ごとにカンマ区切りする（numify 用） */
inline void group_digits(std::string& out, std::string_view num) {
  int digits = static_cast<int>(num.size());
  int groups = (digits - 1) / 3;
  out.resize(static_cast<std::size_t>(digits + groups), '\0');
  int out_pos = static_cast<int>(out.size()) - 1;
  int count = 0;
  for (int i = digits - 1; i >= 0; --i) {
    out[static_cast<std::size_t>(out_pos--)] = num[static_cast<std::size_t>(i)];
    ++count;
    if (count % 3 == 0 && i > 0) out[static_cast<std::size_t>(out_pos--)] = ',';
  }
}

inline void filter_int_numify(std::string& s) {
  bool has_frac = s.find('.') != std::string::npos || s.find('e') != std::string::npos || s.find('E') != std::string::npos;
  if (has_frac) {
    double val = 0.0;
    if (auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), val); ec == std::errc{}) {
      bool negative = val < 0;
      if (negative) val = -val;
      auto int_part = static_cast<long long>(val);
      std::string num = std::to_string(int_part);
      std::string grouped;
      group_digits(grouped, num);
      auto frac = val - static_cast<double>(int_part);
      if (frac != 0.0) {
        auto dot_pos = s.find('.');
        std::size_t prec = 6;
        if (dot_pos != std::string::npos) {
          prec = s.size() - dot_pos - 1;
          if (prec > 6) prec = 6;
          if (prec == 0) prec = 1;
        }
        char buf[64];
        if (auto [ptr, ec2] = std::to_chars(buf, buf + sizeof(buf), frac, std::chars_format::fixed, static_cast<int>(prec)); ec2 == std::errc{}) {
          std::string_view frac_str(buf, static_cast<std::size_t>(ptr - buf));
          if (frac_str.size() > 2) frac_str = frac_str.substr(1);  // "0.x" → ".x"
          grouped += frac_str;
        }
      }
      s = negative ? "-" + grouped : grouped;
    }
  } else {
    long long val = 0;
    if (auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), val); ec == std::errc{}) {
      std::string num = std::to_string(val);
      bool negative = !num.empty() && num[0] == '-';
      if (negative) num.erase(0, 1);
      std::string grouped;
      group_digits(grouped, num);
      s = negative ? "-" + grouped : grouped;
    }
  }
}

inline void filter_int_zerofill(std::string& s, int width) {
  long long val = 0;
  if (auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), val); ec == std::errc{}) {
    auto digits_str = std::to_string(val);
    bool negative = !digits_str.empty() && digits_str[0] == '-';
    if (negative) digits_str.erase(0, 1);
    int total = negative ? static_cast<int>(digits_str.size()) + 1 : static_cast<int>(digits_str.size());
    if (total < width) {
      auto padding = width - total;
      if (negative) s = "-" + std::string(static_cast<std::size_t>(padding), '0') + digits_str;
      else s = std::string(static_cast<std::size_t>(padding), '0') + digits_str;
    }
  }
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
