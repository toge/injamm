/**
 * @file bench_codegen.cpp
 * @brief codegen 生成コードとランタイムエンジンの性能比較ベンチマーク
 *
 * @details examples/codegen_bench/render_{a,b,c}.hpp は injamm_codegen が生成した
 *          C++ レンダリング関数。同じテンプレートをランタイムエンジンと生成コードの
 *          両方でレンダリングし、1 回あたりの処理時間 (ns/op) を比較する。
 *
 *          ワークロード:
 *          - render_a: セクションループ（文字列/数値出力）
 *          - render_b: ループ内フィルタ（_filtered バッファ再利用）
 *          - render_c: ループ内 bool 出力（エスケープ不要パス）
 *
 *          codegen を改良した場合は gen.sh で render_*.hpp を再生成してから
 *          再実行することで、改善前後のスループットを比較できる。
 *
 * 使い方:
 *   ./build/injamm_codegen_bench               # デフォルト（2000 要素, 500 反復）
 *   ./build/injamm_codegen_bench 5000 1000     # 要素数 / 反復回数を指定
 */

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <injamm.hpp>
#include <injamm/escape_hatch.hpp>

#include "render_a.hpp"
#include "render_b.hpp"
#include "render_c.hpp"

// ============================================================
// テストデータ型
// ============================================================

struct Item {
  std::string name;
  int quantity = 0;
  double price = 0.0;
};

struct Data {
  std::string name;
  bool active = true;
  std::vector<Item> items;
};

template <>
struct glz::meta<Item> {
  static constexpr auto value = glz::object("name", &Item::name, "quantity", &Item::quantity, "price", &Item::price);
};

template <>
struct glz::meta<Data> {
  static constexpr auto value = glz::object("name", &Data::name, "active", &Data::active, "items", &Data::items);
};

// ベンチマーク対象テンプレート（gen.sh と同一の文字列を指定すること）
static constexpr char const* T_A = "{{#items}}[{{name}}/{{quantity}}/{{price}}]\n{{/items}}";
static constexpr char const* T_B = "{{#items}}{{name|upper}}{{/items}}";
static constexpr char const* T_C = "{{#items}}{{root.active}}{{/items}}";

volatile std::size_t g_sink = 0;

// ============================================================
// 計測ヘルパ
// ============================================================

/** @brief 反復回数分実行し、1 回あたりの平均処理時間の最小値 (ns) を返す */
template <typename Fn>
double ns_per_op(Fn&& fn, int iters, int reps) {
  fn(); // ウォームアップ
  double best = 1e18;
  for (int r = 0; r < reps; ++r) {
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < iters; ++i) fn();
    auto t1 = std::chrono::steady_clock::now();
    best = std::min(best, std::chrono::duration<double, std::nano>(t1 - t0).count() / static_cast<double>(iters));
  }
  return best;
}

int main(int argc, char** argv) {
  int item_count = 2000;
  int iters = 500;
  if (argc > 1) item_count = std::atoi(argv[1]);
  if (argc > 2) iters = std::atoi(argv[2]);
  constexpr int reps = 7;

  // 名前は SSO (15文字) を超える長さにする（フィルタ時のヒープ確保を再現）
  Data data;
  data.name = "Root-Data-Object-2026";
  data.items.reserve(static_cast<std::size_t>(item_count));
  for (int i = 0; i < item_count; ++i) {
    data.items.push_back({"Widget-Premium-Model-" + std::to_string(i), i % 100, 99.99 + i});
  }

  injamm::engine<Data> eng_a{std::string(T_A)};
  injamm::engine<Data> eng_b{std::string(T_B)};
  injamm::engine<Data> eng_c{std::string(T_C)};

  std::string out;
  out.reserve(1u << 20);

  // 出力の一致確認（ランタイム vs 生成コード）
  {
    std::string a, b, c;
    (void)generated::render_a(data, a);
    auto r1 = eng_a.render(data, b);
    (void)r1;
    if (a != b) std::printf("WARNING: render_a output mismatch (gen %zu / rt %zu)\n", a.size(), b.size());
    (void)generated::render_b(data, a);
    auto r2 = eng_b.render(data, b);
    (void)r2;
    (void)generated::render_c(data, a);
    auto r3 = eng_c.render(data, b);
    (void)r3;
    if (a != b) std::printf("WARNING: render_c output mismatch (gen %zu / rt %zu)\n", a.size(), b.size());
  }

  std::printf("codegen vs runtime (items=%d iters=%d reps=%d, best-of-reps)\n\n", item_count, iters, reps);

  auto report = [](const char* label, double ns) {
    std::printf("  %-16s %10.1f ns/op  (%6.2f Mop/s)\n", label, ns, 1e3 / ns);
  };

  std::printf("[A] section loop (render_a)\n");
  report("runtime", ns_per_op([&] { auto r = eng_a.render(data, out); g_sink += out.size() + (r ? 0 : 1); }, iters, reps));
  report("codegen", ns_per_op([&] { auto r = generated::render_a(data, out); g_sink += out.size() + (r ? 0 : 1); }, iters, reps));

  std::printf("[B] filter in loop (render_b)\n");
  report("runtime", ns_per_op([&] { auto r = eng_b.render(data, out); g_sink += out.size() + (r ? 0 : 1); }, iters, reps));
  report("codegen", ns_per_op([&] { auto r = generated::render_b(data, out); g_sink += out.size() + (r ? 0 : 1); }, iters, reps));

  std::printf("[C] bool output in loop (render_c)\n");
  report("runtime", ns_per_op([&] { auto r = eng_c.render(data, out); g_sink += out.size() + (r ? 0 : 1); }, iters, reps));
  report("codegen", ns_per_op([&] { auto r = generated::render_c(data, out); g_sink += out.size() + (r ? 0 : 1); }, iters, reps));

  std::printf("\nnote: codegen は render(data, out) バッファ再利用 API を使用\n");
  return 0;
}
