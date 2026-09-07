#pragma once

/**
 * @file config.hpp
 * @brief injamm feature-gate macros
 *
 * @details このファイルは必ず他の injamm ヘッダより先にインクルードされる。
 *
 *  ライブラリは既定で例外なしでも動作する（frozenchars と同様）。
 *  実行時APIの失敗は `std::expected<T, error_ctx>` で返る。
 *  wasip1 ビルド時はユーザーが -fno-exceptions を直接指定する。
 *
 *  -fno-exceptions 時に __cpp_exceptions が未定義となり、自動的に例外なしモードになる。
 */

#if !defined(__cpp_exceptions)
// 例外無効 — enchantum のコア API (to_string / cast / contains) は noexcept
// のため例外なしでも動作する。array::at / bitset::test|set|reset|flip のみが
// throw するため ENCHANTUM_THROW を trap に差し替えて -fno-exceptions でも
// コンパイル可能にする。std::format / fmt の vformat も -fno-exceptions では
// _GLIBCXX_THROW_OR_ABORT / assert_fail で abort/trap にフォールバックする
// ため format フィルタは例外なしでも維持可能（不正フォーマットは trap）。

#define INJAMM_HAS_EXCEPTIONS 0

#include <cstdlib>

namespace injamm::detail {
[[noreturn]] inline void injamm_trap() noexcept {
#if defined(__wasm__)
#if defined(__has_builtin)
#if __has_builtin(__builtin_trap)
  __builtin_trap();
#else
  std::abort();
#endif
#elif defined(__GNUC__) || defined(__clang__)
  __builtin_trap();
#else
  std::abort();
#endif
#else
  std::abort();
#endif
}
}  // namespace injamm::detail

#define INJAMM_THROW(...) (::injamm::detail::injamm_trap())
#ifndef ENCHANTUM_THROW
#define ENCHANTUM_THROW(exception, ...) (::injamm::detail::injamm_trap())
#endif

#else

#define INJAMM_HAS_EXCEPTIONS 1

#include <cstdlib>
#include <stdexcept>
#define INJAMM_THROW(...) throw __VA_ARGS__

namespace injamm::detail {
[[noreturn]] inline void injamm_trap() noexcept { std::abort(); }
}  // namespace injamm::detail

#endif

// glaze (7.8.3+ / 8.3.0でも未修正) の atoi.hpp full_multiplication は 32bit 非 MSVC 環境で
// MSVC 組み込みの未修飾 `_umul128` を呼ぶため wasm32 (wasip1 / emscripten) では
// 未宣言エラーになる。fast_float::_umul128 相当をグローバルに補って回避する。
// ponytail: glaze 上流の atoi.hpp が修正されたら削除
#if defined(__wasm__) && defined(__SIZEOF_INT128__)
#include <cstdint>
inline constexpr std::uint64_t _umul128(std::uint64_t a, std::uint64_t b, std::uint64_t* hi) noexcept {
  auto const r = static_cast<unsigned __int128>(a) * b;
  *hi = static_cast<std::uint64_t>(r >> 64);
  return static_cast<std::uint64_t>(r);
}
#endif
