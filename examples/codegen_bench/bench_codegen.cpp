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
 *          - render_d: ループ内逆セクション（半分の要素で本体実行）
 *          - render_e: ループ内スカラーセクション + {{else}}
 *          - render_f: std::optional のセクション
 *          - render_g: ループ内グローバル partial（値 → ルートの二段解決）
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
#include <optional>
#include <string>
#include <vector>

#include <injamm.hpp>
#include <injamm/escape_hatch.hpp>

#include "render_a.hpp"
#include "render_b.hpp"
#include "render_c.hpp"
#include "render_d.hpp"
#include "render_e.hpp"
#include "render_f.hpp"
#include "render_g.hpp"

// ============================================================
// テストデータ型
// ============================================================

struct Item {
  std::string name;
  std::string note;   // {{^note}} 用（半分の要素を空にする）
  bool active = true; // {{#active}}...{{else}} 用（半分の要素を false にする）
  int quantity = 0;
  double price = 0.0;
};

struct Data {
  std::string name;
  bool active = true;
  std::optional<std::string> opt = std::string{"optional-value"};
  std::vector<Item> items;
};

template <>
struct glz::meta<Item> {
  static constexpr auto value = glz::object("name", &Item::name, "note", &Item::note, "active", &Item::active, "quantity", &Item::quantity, "price", &Item::price);
};

template <>
struct glz::meta<Data> {
  static constexpr auto value = glz::object("name", &Data::name, "active", &Data::active, "opt", &Data::opt, "items", &Data::items);
};

// ベンチマーク対象テンプレート（gen.sh と同一の文字列を指定すること）
static constexpr char const* T_A = "{{#items}}[{{name}}/{{quantity}}/{{price}}]\n{{/items}}";
static constexpr char const* T_B = "{{#items}}{{name|upper}}{{/items}}";
static constexpr char const* T_C = "{{#items}}{{root.active}}{{/items}}";
static constexpr char const* T_D = "{{#items}}{{^note}}x{{/note}}{{/items}}";
static constexpr char const* T_E = "{{#items}}{{#active}}Y{{else}}N{{/active}}{{/items}}";
static constexpr char const* T_F = "{{#opt}}[{{this}}]{{/opt}}";
static constexpr char const* T_G = "{{#partialdef g}}{{name}};{{/partialdef}}{{#items}}{{>g}}{{/items}}";

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
  // note / active は半分の要素で空/false にし、逆セクション・else 節が実行されるワークロードにする
  Data data;
  data.name = "Root-Data-Object-2026";
  data.items.reserve(static_cast<std::size_t>(item_count));
  for (int i = 0; i < item_count; ++i) {
    data.items.push_back({"Widget-Premium-Model-" + std::to_string(i),
                          (i % 2 == 0) ? "" : "note-" + std::to_string(i),
                          (i % 2 == 0),
                          i % 100, 99.99 + i});
  }

  injamm::engine<Data> eng_a{std::string(T_A)};
  injamm::engine<Data> eng_b{std::string(T_B)};
  injamm::engine<Data> eng_c{std::string(T_C)};
  injamm::engine<Data> eng_d{std::string(T_D)};
  injamm::engine<Data> eng_e{std::string(T_E)};
  injamm::engine<Data> eng_f{std::string(T_F)};
  injamm::engine<Data> eng_g{std::string(T_G)};

  std::string out;
  out.reserve(1u << 20);

  // 出力の一致確認（ランタイム vs 生成コード）
  {
    std::string a, b;
    auto check = [&](auto const& gen, injamm::engine<Data>& eng, char const* label) {
      a.clear();
      (void)gen(data, a);
      b.clear();
      (void)eng.render(data, b);
      if (a != b) std::printf("WARNING: %s output mismatch (gen %zu / rt %zu)\n", label, a.size(), b.size());
    };
    check([](auto const& d, std::string& o) { return generated::render_a(d, o); }, eng_a, "render_a");
    check([](auto const& d, std::string& o) { return generated::render_b(d, o); }, eng_b, "render_b");
    check([](auto const& d, std::string& o) { return generated::render_c(d, o); }, eng_c, "render_c");
    check([](auto const& d, std::string& o) { return generated::render_d(d, o); }, eng_d, "render_d");
    check([](auto const& d, std::string& o) { return generated::render_e(d, o); }, eng_e, "render_e");
    check([](auto const& d, std::string& o) { return generated::render_f(d, o); }, eng_f, "render_f");
    check([](auto const& d, std::string& o) { return generated::render_g(d, o); }, eng_g, "render_g");
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

  std::printf("[D] inverted in loop (render_d)\n");
  report("runtime", ns_per_op([&] { auto r = eng_d.render(data, out); g_sink += out.size() + (r ? 0 : 1); }, iters, reps));
  report("codegen", ns_per_op([&] { auto r = generated::render_d(data, out); g_sink += out.size() + (r ? 0 : 1); }, iters, reps));

  std::printf("[E] section else in loop (render_e)\n");
  report("runtime", ns_per_op([&] { auto r = eng_e.render(data, out); g_sink += out.size() + (r ? 0 : 1); }, iters, reps));
  report("codegen", ns_per_op([&] { auto r = generated::render_e(data, out); g_sink += out.size() + (r ? 0 : 1); }, iters, reps));

  std::printf("[F] optional section (render_f)\n");
  report("runtime", ns_per_op([&] { auto r = eng_f.render(data, out); g_sink += out.size() + (r ? 0 : 1); }, iters, reps));
  report("codegen", ns_per_op([&] { auto r = generated::render_f(data, out); g_sink += out.size() + (r ? 0 : 1); }, iters, reps));

  std::printf("[G] partial in loop (render_g)\n");
  report("runtime", ns_per_op([&] { auto r = eng_g.render(data, out); g_sink += out.size() + (r ? 0 : 1); }, iters, reps));
  report("codegen", ns_per_op([&] { auto r = generated::render_g(data, out); g_sink += out.size() + (r ? 0 : 1); }, iters, reps));

  std::printf("\nnote: codegen は render(data, out) バッファ再利用 API を使用\n");
  return 0;
}
