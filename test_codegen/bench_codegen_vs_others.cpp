/**
 * @file bench_codegen_vs_others.cpp
 * @brief codegen 版 / CT 版 / ランタイム版の性能比較
 */

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include <injamm.hpp>
#include <injamm/escape_hatch.hpp>

// ============================================================
// テストデータ型
// ============================================================

struct BenchData {
  std::string name;
  int age = 0;
};

template <>
struct glz::meta<BenchData> {
  static constexpr auto value = glz::object("name", &BenchData::name, "age", &BenchData::age);
};

// ============================================================
// 生成コード
// ============================================================

namespace gen {
#include "render_bench.hpp"
}

// ============================================================
// ベンチマーク
// ============================================================

static double elapsed_us(auto const& start, auto const& end) {
  return std::chrono::duration<double, std::micro>(end - start).count();
}

int main() {
  BenchData data{"Alice", 30};

  std::printf("=== codegen vs CT vs Runtime 比較ベンチマーク ===\n\n");

  // --- ランタイム版（コンパイル含む） ---
  constexpr int ITERS = 100000;
  {
    // コンパイル時間を計測
    auto start_compile = std::chrono::high_resolution_clock::now();
    injamm::engine<BenchData> runtime_eng{"{{name|upper}} (age={{age}}): {{#if age > 20}}adult{{else}}minor{{/if}}"};
    auto end_compile = std::chrono::high_resolution_clock::now();
    auto compile_us = elapsed_us(start_compile, end_compile);

    // ウォームアップ
    for (int i = 0; i < 10000; ++i)
      (void)runtime_eng.render(data);

    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITERS; ++i)
      (void)runtime_eng.render(data);
    auto end = std::chrono::high_resolution_clock::now();
    auto render_us = elapsed_us(start, end);
    std::printf("  runtime  compile: %8.0f us\n", compile_us);
    std::printf("  runtime  render:  %8.0f us  (%6.1f ns/call)\n", render_us, render_us * 1000.0 / ITERS);
    std::printf("  runtime  total:   %8.0f us  (compile + %d renders)\n", compile_us + render_us, ITERS);
  }

  // --- CT 版 ---
  {
    constexpr auto kTmpl = injamm::fixed_string("{{name|upper}} (age={{age}}): {{#if age > 20}}adult{{else}}minor{{/if}}");

    // コンパイル時間はゼロ（テンプレート引数でコンパイル時展開済み）
    std::printf("\n  ct       compile:        0 us  (テンプレート引数でコンパイル時展開)\n");

    for (int i = 0; i < 10000; ++i)
      (void)injamm::render<kTmpl>(data);

    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITERS; ++i)
      (void)injamm::render<kTmpl>(data);
    auto end = std::chrono::high_resolution_clock::now();
    auto render_us = elapsed_us(start, end);
    std::printf("  ct       render:  %8.0f us  (%6.1f ns/call)\n", render_us, render_us * 1000.0 / ITERS);
    std::printf("  ct       total:   %8.0f us\n", render_us);
  }

  // --- codegen 版 ---
  {
    // コンパイル時間はゼロ（事前に C++ コード生成済み）
    std::printf("\n  codegen  compile:        0 us  (事前に C++ コード生成 + ビルド済み)\n");

    for (int i = 0; i < 10000; ++i)
      (void)gen::generated::render(data);

    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITERS; ++i)
      (void)gen::generated::render(data);
    auto end = std::chrono::high_resolution_clock::now();
    auto render_us = elapsed_us(start, end);
    std::printf("  codegen  render:  %8.0f us  (%6.1f ns/call)\n", render_us, render_us * 1000.0 / ITERS);
    std::printf("  codegen  total:   %8.0f us\n", render_us);
  }

  // --- 正常性確認 ---
  std::printf("\n--- 正常性確認 ---\n");
  {
    injamm::engine<BenchData> v_eng{"{{name|upper}} (age={{age}}): {{#if age > 20}}adult{{else}}minor{{/if}}"};
    constexpr auto v_kTmpl = injamm::fixed_string("{{name|upper}} (age={{age}}): {{#if age > 20}}adult{{else}}minor{{/if}}");
    auto r_runtime = v_eng.render(data);
    auto r_ct = injamm::render<v_kTmpl>(data);
    auto r_codegen = gen::generated::render(data);

    std::printf("  runtime:  %s\n", r_runtime->c_str());
    std::printf("  ct:       %s\n", r_ct->c_str());
    std::printf("  codegen:  %s\n", r_codegen->c_str());

    bool ok = (*r_runtime == *r_codegen);
    std::printf("  runtime == codegen: %s\n", ok ? "YES" : "NO");
    if (!ok) {
      std::printf("  runtime size=%zu hex=", r_runtime->size());
      for (auto c : *r_runtime) std::printf("%02x", (unsigned char)c);
      std::printf("\n  codegen size=%zu hex=", r_codegen->size());
      for (auto c : *r_codegen) std::printf("%02x", (unsigned char)c);
      std::printf("\n");
    }
  }
  std::printf("\n=== done ===\n");
  return 0;
}
