#pragma once

/**
 * @file config.hpp
 * @brief injamm feature-gate macros
 *
 * @details このファイルは必ず他の injamm ヘッダより先にインクルードされる。
 *
 *  INJAMM_FREESTANDING が定義されると以下のサブ機能が自動的に無効化される:
 *   - INJAMM_NO_BYTECODE_IO : <istream>/<ostream> ベースのバイトコード保存/読み込み
 *   - INJAMM_NO_CHRONO      : <chrono>/<ctime> ベースの time_point シリアライズ
 *   - INJAMM_NO_FMT         : <format>/<fmt/format.h> ベースの format フィルタ
 *
 *  wasm32-unknown-unknown (freestanding, -nostdlib) では __wasm__ が定義され
 *  __wasi__ が未定義のため自動的に INJAMM_FREESTANDING が有効になる。
 *  emscripten (__EMSCRIPTEN__ 定義) は完全な libc++ を持つホスト環境のため対象外。
 *  CMake の ENABLE_FREESTANDING オプションからも明示的に制御できる。
 */

// wasm32-unknown-unknown を自動検出 (emscripten はホスト環境として除外)
#if !defined(INJAMM_FREESTANDING) && defined(__wasm__) && !defined(__wasi__) && !defined(__EMSCRIPTEN__)
#define INJAMM_FREESTANDING 1
#endif

#ifdef INJAMM_FREESTANDING
#ifndef INJAMM_NO_BYTECODE_IO
#define INJAMM_NO_BYTECODE_IO
#endif
#ifndef INJAMM_NO_CHRONO
#define INJAMM_NO_CHRONO
#endif
#ifndef INJAMM_NO_FMT
#define INJAMM_NO_FMT
#endif
// enchantum / enum レジストリも無効化
#ifndef INJAMM_NO_ENUM_REGISTRY
#define INJAMM_NO_ENUM_REGISTRY
#endif
#endif
