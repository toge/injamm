#pragma once

/**
 * @file config.hpp
 * @brief injamm feature-gate macros
 *
 * @details このファイルは必ず他の injamm ヘッダより先にインクルードされる。
 *
 *  INJAMM_WASI_MINIMAL は wasm32-wasip1 + wasi-sdk の hosted (WASI) を想定し、
 *  無効化するのは例外のみ。wasm32-wasip1 では WASI 経由で <iostream> (<istream>/
 *  <ostream>) が利用可能なため INJAMM_NO_BYTECODE_IO は定義しない。
 *
 *  自動検出は行わない。CMake の ENABLE_WASI_MINIMAL=ON または -DINJAMM_WASI_MINIMAL
 *  で明示的に有効化する。wasm32-unknown-unknown (freestanding, -nostdlib) は
 *  hosted stdlib (glaze, <string> 等) に依存するため非対応。
 *
 *  ENABLE_WASI_MINIMAL=ON のときのみ例外を無効化する（-fno-exceptions）。
 *  通常ビルド（OFF）では例外有効で Catch2 テストが実行可能。
 */

#if defined(INJAMM_WASI_MINIMAL) && !defined(INJAMM_NO_EXCEPTIONS)
#define INJAMM_NO_EXCEPTIONS
#endif

#if defined(INJAMM_NO_EXCEPTIONS) || !defined(__cpp_exceptions)
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
#if defined(__wasm__) || defined(INJAMM_WASI_MINIMAL)
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
