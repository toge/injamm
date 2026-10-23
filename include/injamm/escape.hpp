#pragma once

#include "config.hpp"
#include <string_view>

#if defined(__AVX2__)
#include <immintrin.h>
#elif defined(__SSE2__)
#include <emmintrin.h>
#elif defined(__ARM_NEON)
#include <arm_neon.h>
#endif

namespace injamm::detail {

/**
 * @brief 出力バッファへエスケープ済み文字列を追記する（例外利用時は通常の append）。
 * @details INJAMM_HAS_EXCEPTIONS 時は try/catch を境界で受けるため zero-cost append を維持する。
 *          !INJAMM_HAS_EXCEPTIONS 時は、追記前に残容量を検査し、不足時は false を返す
 *          ことで例外 trap を避ける。
 */
#if INJAMM_HAS_EXCEPTIONS
template <class Buffer>
constexpr bool html_escape_append(Buffer& out, std::string_view const& sv) {
  out.append(sv);
  return true;
}

template <class Buffer>
constexpr bool html_escape_append(Buffer& out, const char* data, std::size_t n) {
  out.append(data, n);
  return true;
}
#else
template <class Buffer>
constexpr bool html_escape_append(Buffer& out, std::string_view const& sv) noexcept {
  if (sv.size() > out.capacity() - out.size()) return false;
  out.append(sv);
  return true;
}

template <class Buffer>
constexpr bool html_escape_append(Buffer& out, const char* data, std::size_t n) noexcept {
  if (static_cast<std::size_t>(n) > out.capacity() - out.size()) return false;
  out.append(data, n);
  return true;
}
#endif

// スカラー版 HTML エスケープのフォールバック実装。constexpr-safe（html_escape_append と switch のみ使用）。
template <class Buffer>
constexpr bool html_escape_scalar(Buffer& out, std::string_view s) {
  std::size_t safe = 0;
  for (std::size_t i = 0; i < s.size(); ++i) {
    auto const c = s[i];
    if (c != '<' && c != '>' && c != '&' && c != '"' && c != '\'')
      continue;
    if (i > safe)
      if (!html_escape_append(out, s.data() + safe, i - safe)) return false;
    switch (c) {
    case '<':
      if (!html_escape_append(out, "&lt;")) return false;
      break;
    case '>':
      if (!html_escape_append(out, "&gt;")) return false;
      break;
    case '&':
      if (!html_escape_append(out, "&amp;")) return false;
      break;
    case '"':
      if (!html_escape_append(out, "&quot;")) return false;
      break;
    case '\'':
      if (!html_escape_append(out, "&#x27;")) return false;
      break;
    }
    safe = i + 1;
  }
  if (safe < s.size()) {
    if (!html_escape_append(out, s.data() + safe, s.size() - safe)) return false;
  }
  return true;
}

#if defined(__AVX2__)
template <class Buffer>
constexpr bool html_escape_into(Buffer& out, std::string_view s) {
  if !consteval {
    std::size_t i    = 0;
    std::size_t len  = s.size();
    auto const* data = s.data();

    auto const process_special = [&](std::size_t start, std::size_t end) -> bool {
      std::size_t safe = start;
      for (std::size_t j = start; j < end; ++j) {
        auto const c = data[j];
        if (c != '<' && c != '>' && c != '&' && c != '"' && c != '\'') continue;
        if (j > safe) if (!html_escape_append(out, data + safe, j - safe)) return false;
        switch (c) {
        case '<': if (!html_escape_append(out, "&lt;")) return false;   break;
        case '>': if (!html_escape_append(out, "&gt;")) return false;   break;
        case '&': if (!html_escape_append(out, "&amp;")) return false;  break;
        case '"': if (!html_escape_append(out, "&quot;")) return false; break;
        case '\'': if (!html_escape_append(out, "&#x27;")) return false; break;
        }
        safe = j + 1;
      }
      if (safe < end) if (!html_escape_append(out, data + safe, end - safe)) return false;
      return true;
    };

    auto const find_first_match = [](unsigned mask) -> int {
#if defined(__GNUC__) || defined(__clang__)
      return __builtin_ctz(mask);
#else
      unsigned long index;
      if (_BitScanForward(&index, mask)) return static_cast<int>(index);
      return 32;
#endif
    };

    while (i + 32 <= len) {
      __m256i chunk = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(data + i));
      __m256i lt    = _mm256_cmpeq_epi8(chunk, _mm256_set1_epi8('<'));
      __m256i gt    = _mm256_cmpeq_epi8(chunk, _mm256_set1_epi8('>'));
      __m256i amp   = _mm256_cmpeq_epi8(chunk, _mm256_set1_epi8('&'));
      __m256i dqt   = _mm256_cmpeq_epi8(chunk, _mm256_set1_epi8('"'));
      __m256i sqt   = _mm256_cmpeq_epi8(chunk, _mm256_set1_epi8('\''));

      __m256i any   = _mm256_or_si256(_mm256_or_si256(lt, gt), _mm256_or_si256(amp, _mm256_or_si256(dqt, sqt)));
      unsigned mask = static_cast<unsigned>(_mm256_movemask_epi8(any));

      if (mask == 0) {
        if (!html_escape_append(out, data + i, 32)) return false;
        i += 32;
      } else {
        int first = find_first_match(mask);
        if (first > 0) {
          if (!html_escape_append(out, data + i, first)) return false;
        }
        if (!process_special(i + first, i + 32)) return false;
        i += 32;
      }
    }

    if (i < len) {
      if (!process_special(i, len)) return false;
    }
  } else {
    if (!html_escape_scalar(out, s)) return false;
  }
  return true;
}
#elif defined(__SSE2__)
template <class Buffer>
constexpr bool html_escape_into(Buffer& out, std::string_view s) {
  if !consteval {
    std::size_t i    = 0;
    std::size_t len  = s.size();
    auto const* data = s.data();

    auto const process_special = [&](std::size_t start, std::size_t end) -> bool {
      std::size_t safe = start;
      for (std::size_t j = start; j < end; ++j) {
        auto const c = data[j];
        if (c != '<' && c != '>' && c != '&' && c != '"' && c != '\'') continue;
        if (j > safe) if (!html_escape_append(out, data + safe, j - safe)) return false;
        switch (c) {
        case '<': if (!html_escape_append(out, "&lt;")) return false;   break;
        case '>': if (!html_escape_append(out, "&gt;")) return false;   break;
        case '&': if (!html_escape_append(out, "&amp;")) return false;  break;
        case '"': if (!html_escape_append(out, "&quot;")) return false; break;
        case '\'': if (!html_escape_append(out, "&#x27;")) return false; break;
        }
        safe = j + 1;
      }
      if (safe < end) if (!html_escape_append(out, data + safe, end - safe)) return false;
      return true;
    };

    auto const find_first_match = [](unsigned mask) -> int {
#if defined(__GNUC__) || defined(__clang__)
      return __builtin_ctz(mask);
#else
      for (int b = 0; b < 16; ++b) {
        if (mask & (1u << b))
          return b;
      }
      return 16;
#endif
    };

    while (i + 16 <= len) {
      __m128i chunk = _mm_loadu_si128(reinterpret_cast<const __m128i*>(data + i));
      __m128i lt    = _mm_cmpeq_epi8(chunk, _mm_set1_epi8('<'));
      __m128i gt    = _mm_cmpeq_epi8(chunk, _mm_set1_epi8('>'));
      __m128i amp   = _mm_cmpeq_epi8(chunk, _mm_set1_epi8('&'));
      __m128i dqt   = _mm_cmpeq_epi8(chunk, _mm_set1_epi8('"'));
      __m128i sqt   = _mm_cmpeq_epi8(chunk, _mm_set1_epi8('\''));

      __m128i  any  = _mm_or_si128(_mm_or_si128(lt, gt), _mm_or_si128(amp, _mm_or_si128(dqt, sqt)));
      unsigned mask = static_cast<unsigned>(_mm_movemask_epi8(any));

      if (mask == 0) {
        if (!html_escape_append(out, data + i, 16)) return false;
        i += 16;
      } else {
        int first = find_first_match(mask);
        if (first > 0) {
          if (!html_escape_append(out, data + i, first)) return false;
        }
        if (!process_special(i + first, i + 16)) return false;
        i += 16;
      }
    }

    if (i < len) {
      if (!process_special(i, len)) return false;
    }
  } else {
    if (!html_escape_scalar(out, s)) return false;
  }
  return true;
}
#elif defined(__ARM_NEON)
template <class Buffer>
constexpr bool html_escape_into(Buffer& out, std::string_view s) {
  if !consteval {
    std::size_t i    = 0;
    std::size_t len  = s.size();
    auto const* data = s.data();

    auto const process_special = [&](std::size_t start, std::size_t end) -> bool {
      std::size_t safe = start;
      for (std::size_t j = start; j < end; ++j) {
        auto const c = data[j];
        if (c != '<' && c != '>' && c != '&' && c != '"' && c != '\'') continue;
        if (j > safe) if (!html_escape_append(out, data + safe, j - safe)) return false;
        switch (c) {
        case '<': if (!html_escape_append(out, "&lt;")) return false;   break;
        case '>': if (!html_escape_append(out, "&gt;")) return false;   break;
        case '&': if (!html_escape_append(out, "&amp;")) return false;  break;
        case '"': if (!html_escape_append(out, "&quot;")) return false; break;
        case '\'': if (!html_escape_append(out, "&#x27;")) return false; break;
        }
        safe = j + 1;
      }
      if (safe < end) if (!html_escape_append(out, data + safe, end - safe)) return false;
      return true;
    };

    auto const neon_movemask = [](uint8x16_t v) -> uint16_t {
      static const uint8_t mask_data[16] __attribute__((aligned(16))) = {
        1, 2, 4, 8, 16, 32, 64, 128,
        1, 2, 4, 8, 16, 32, 64, 128
      };
      uint8x16_t mask = vld1q_u8(mask_data);
      uint8x16_t masked = vandq_u8(v, mask);
      uint16x8_t wide = vpaddlq_u8(masked);
      uint32x4_t wider = vpaddlq_u16(wide);
      uint64x2_t widest = vpaddlq_u32(wider);
      return static_cast<uint16_t>(vgetq_lane_u64(widest, 0) | (vgetq_lane_u64(widest, 1) << 8));
    };

    auto const find_first_match = [](uint16_t mask) -> int {
#if defined(__GNUC__) || defined(__clang__)
      return __builtin_ctz(mask);
#else
      for (int b = 0; b < 16; ++b)
        if (mask & (1u << b)) return b;
      return 16;
#endif
    };

    while (i + 16 <= len) {
      uint8x16_t chunk = vld1q_u8(reinterpret_cast<const uint8_t*>(data + i));
      uint8x16_t lt    = vceqq_u8(chunk, vdupq_n_u8('<'));
      uint8x16_t gt    = vceqq_u8(chunk, vdupq_n_u8('>'));
      uint8x16_t amp   = vceqq_u8(chunk, vdupq_n_u8('&'));
      uint8x16_t dqt   = vceqq_u8(chunk, vdupq_n_u8('"'));
      uint8x16_t sqt   = vceqq_u8(chunk, vdupq_n_u8('\''));

      uint8x16_t any   = vorrq_u8(vorrq_u8(lt, gt), vorrq_u8(amp, vorrq_u8(dqt, sqt)));
      uint16_t   mask  = neon_movemask(any);

      if (mask == 0) {
        if (!html_escape_append(out, data + i, 16)) return false;
        i += 16;
      } else {
        int first = find_first_match(mask);
        if (first > 0) {
          if (!html_escape_append(out, data + i, first)) return false;
        }
        if (!process_special(i + first, i + 16)) return false;
        i += 16;
      }
    }

    if (i < len) {
      if (!process_special(i, len)) return false;
    }
  } else {
    if (!html_escape_scalar(out, s)) return false;
  }
  return true;
}
#else
/** @brief HTMLエスケープをバッファに書き込む
 *
 * @tparam Buffer 出力バッファ型（std::string など）
 * @param[in,out] out 出力先バッファ
 * @param[in] s HTMLエスケープする文字列
 *
 * @details <, >, &, ", ' の5文字をそれぞれのHTMLエンティティに変換して追記する。
 *  特殊文字を含まない連続区間は一括 append し、push_back の呼び出し回数を削減する。
 */
template <class Buffer>
constexpr bool html_escape_into(Buffer& out, std::string_view s) {
  return html_escape_scalar(out, s);
}
#endif

}  // namespace injamm::detail
