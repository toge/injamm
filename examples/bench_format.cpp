#include "injamm/escape_hatch.hpp"
#include <chrono>
#include <cstdio>
#include <format>
#include <iterator>
#include <string>
#ifdef INJAMM_BENCH_FMT
#include <fmt/compile.h>
#endif

// ---- データ型（ループなし・変数置換のみの単純テンプレート用） ----

struct Data1 {
  std::string val;
};

template <>
struct glz::meta<Data1> {
  using T                     = Data1;
  static constexpr auto value = object(&T::val);
};

struct Data3 {
  std::string aaa;
  int         bbb;
  double      ccc;
};

template <>
struct glz::meta<Data3> {
  using T                     = Data3;
  static constexpr auto value = object(&T::aaa, &T::bbb, &T::ccc);
};

struct Data1i {
  int val;
};

template <>
struct glz::meta<Data1i> {
  using T                     = Data1i;
  static constexpr auto value = object(&T::val);
};

struct Data1d {
  double val;
};

template <>
struct glz::meta<Data1d> {
  using T                     = Data1d;
  static constexpr auto value = object(&T::val);
};

struct Data2si {
  std::string aaa;
  int         bbb;
};

template <>
struct glz::meta<Data2si> {
  using T                     = Data2si;
  static constexpr auto value = object(&T::aaa, &T::bbb);
};

struct Data2sd {
  std::string aaa;
  double      ccc;
};

template <>
struct glz::meta<Data2sd> {
  using T                     = Data2sd;
  static constexpr auto value = object(&T::aaa, &T::ccc);
};

struct Data2id {
  int    bbb;
  double ccc;
};

template <>
struct glz::meta<Data2id> {
  using T                     = Data2id;
  static constexpr auto value = object(&T::bbb, &T::ccc);
};

struct Data10 {
  std::string a0, a1, a2, a3, a4;
  int         b0, b1, b2;
  double      c0, c1;
};

template <>
struct glz::meta<Data10> {
  using T                     = Data10;
  static constexpr auto value = object(&T::a0, &T::a1, &T::a2, &T::a3, &T::a4, &T::b0, &T::b1, &T::b2, &T::c0, &T::c1);
};

// ---- ベンチ用: 混在テンプレート（直線区間 + セクション）----

struct BenchItem {
  std::string name;
  int         price;
};

template <>
struct glz::meta<BenchItem> {
  using T                     = BenchItem;
  static constexpr auto value = object(&T::name, &T::price);
};

struct BenchCatalog {
  std::string             title;
  std::vector<BenchItem> items;
};

template <>
struct glz::meta<BenchCatalog> {
  using T                     = BenchCatalog;
  static constexpr auto value = object(&T::title, &T::items);
};

// ---- ヘルパー ----

static double elapsed_us(auto const& start, auto const& end) {
  return std::chrono::duration<double, std::micro>(end - start).count();
}

template <typename F>
static double bench(char const* label, int iters, F&& f) {
  auto start = std::chrono::high_resolution_clock::now();
  for (int i = 0; i < iters; ++i)
    (void)f();
  auto end = std::chrono::high_resolution_clock::now();
  auto ns  = elapsed_us(start, end) * 1000.0 / iters;
  std::printf("  %-38s x %6d: %7.1f ns/call\n", label, iters, ns);
  return ns;
}

static void ratio_row(char const* label, double format_ns, double injamm_ns) {
  std::printf("  %-38s  format %7.1f ns   injamm %7.1f ns   ratio %5.2fx  (%s)\n", label, format_ns, injamm_ns, format_ns / injamm_ns, format_ns < injamm_ns ? "format faster" : "injamm faster");
}

int main() {
  std::printf("=== benchmark: injamm (simple, no loops) vs std::format ===\n");
  std::printf("note: {{var}} HTML-escapes; std::format does not. raw compare uses {{{var}}}.\n\n");

  // ---- 1 variable (string) ----
  std::printf("--- 1 var (string) ---\n");
  Data1 const d1{"hello world"};
  auto constexpr kTmpl1 = injamm::fixed_string("{{val}}");
  for (int i = 0; i < 1000; ++i) {
    (void)injamm::render<kTmpl1>(d1);
    (void)std::format("{}", d1.val);
  }
  constexpr int ITERS1 = 200000;
  double        nt1    = bench("injamm NTTP render<kTmpl1>", ITERS1, [&] { return injamm::render<kTmpl1>(d1); });
  double        fmt1   = bench("std::format", ITERS1, [&] { return std::format("{}", d1.val); });

  // ---- 1 variable (int) ----
  std::printf("\n--- 1 var (int) ---\n");
  Data1i const di{503};
  auto constexpr kTmpl1i = injamm::fixed_string("{{val}}");
  for (int i = 0; i < 1000; ++i) {
    (void)injamm::render<kTmpl1i>(di);
    (void)std::format("{}", di.val);
  }
  constexpr int ITERS1I = 200000;
  double        nt1i    = bench("injamm NTTP render<kTmpl1i>", ITERS1I, [&] { return injamm::render<kTmpl1i>(di); });
  double        fmt1i   = bench("std::format", ITERS1I, [&] { return std::format("{}", di.val); });

  // ---- 1 variable (double) ----
  std::printf("\n--- 1 var (double) ---\n");
  Data1d const dd{35.3};
  auto constexpr kTmpl1d = injamm::fixed_string("{{val}}");
  for (int i = 0; i < 1000; ++i) {
    (void)injamm::render<kTmpl1d>(dd);
    (void)std::format("{}", dd.val);
  }
  constexpr int ITERS1D = 200000;
  double        nt1d    = bench("injamm NTTP render<kTmpl1d>", ITERS1D, [&] { return injamm::render<kTmpl1d>(dd); });
  double        fmt1d   = bench("std::format", ITERS1D, [&] { return std::format("{}", dd.val); });

  // ---- 2 variables (string, int) ----
  std::printf("\n--- 2 vars (string, int) ---\n");
  Data2si const d2si{"5", 503};
  auto constexpr kTmpl2si = injamm::fixed_string("test example, {{aaa}} = {{bbb}}");
  for (int i = 0; i < 1000; ++i) {
    (void)injamm::render<kTmpl2si>(d2si);
    (void)std::format("test example, {} = {}", d2si.aaa, d2si.bbb);
  }
  constexpr int ITERS2 = 200000;
  double        nt2si  = bench("injamm NTTP render<kTmpl2si>", ITERS2, [&] { return injamm::render<kTmpl2si>(d2si); });
  double        fmt2si = bench("std::format", ITERS2, [&] { return std::format("test example, {} = {}", d2si.aaa, d2si.bbb); });

  // ---- 2 variables (string, double) ----
  std::printf("\n--- 2 vars (string, double) ---\n");
  Data2sd const d2sd{"5", 35.3};
  auto constexpr kTmpl2sd = injamm::fixed_string("test example, {{aaa}} on {{ccc}}");
  for (int i = 0; i < 1000; ++i) {
    (void)injamm::render<kTmpl2sd>(d2sd);
    (void)std::format("test example, {} on {}", d2sd.aaa, d2sd.ccc);
  }
  double nt2sd  = bench("injamm NTTP render<kTmpl2sd>", ITERS2, [&] { return injamm::render<kTmpl2sd>(d2sd); });
  double fmt2sd = bench("std::format", ITERS2, [&] { return std::format("test example, {} on {}", d2sd.aaa, d2sd.ccc); });

  // ---- 2 variables (int, double) ----
  std::printf("\n--- 2 vars (int, double) ---\n");
  Data2id const d2id{503, 35.3};
  auto constexpr kTmpl2id = injamm::fixed_string("test example, {{bbb}} on {{ccc}}");
  for (int i = 0; i < 1000; ++i) {
    (void)injamm::render<kTmpl2id>(d2id);
    (void)std::format("test example, {} on {}", d2id.bbb, d2id.ccc);
  }
  double nt2id  = bench("injamm NTTP render<kTmpl2id>", ITERS2, [&] { return injamm::render<kTmpl2id>(d2id); });
  double fmt2id = bench("std::format", ITERS2, [&] { return std::format("test example, {} on {}", d2id.bbb, d2id.ccc); });

  // ---- 3 variables (string, int, double) ----
  std::printf("\n--- 3 vars (string, int, double) ---\n");
  Data3 const d3{"5", 503, 35.3};
  auto constexpr kTmpl3 = injamm::fixed_string("test example, {{aaa}} = {{bbb}} on {{ccc}}");
  for (int i = 0; i < 1000; ++i) {
    (void)injamm::render<kTmpl3>(d3);
    (void)std::format("test example, {} = {} on {}", d3.aaa, d3.bbb, d3.ccc);
  }
  constexpr int ITERS3 = 200000;
  double        nt3    = bench("injamm NTTP render<kTmpl3>", ITERS3, [&] { return injamm::render<kTmpl3>(d3); });
  double        fmt3   = bench("std::format", ITERS3, [&] { return std::format("test example, {} = {} on {}", d3.aaa, d3.bbb, d3.ccc); });

  // ---- 10 variables (5 string + 3 int + 2 double) ----
  std::printf("\n--- 10 vars (5 string + 3 int + 2 double) ---\n");
  Data10 d10;
  d10.a0                 = "xyz";
  d10.a1                 = "abc";
  d10.a2                 = "def";
  d10.a3                 = "ghi";
  d10.a4                 = "jkl";
  d10.b0                 = 10;
  d10.b1                 = 20;
  d10.b2                 = 30;
  d10.c0                 = 1.5;
  d10.c1                 = 2.5;
  auto constexpr kTmpl10 = injamm::fixed_string("{{a0}}={{a1}}={{a2}}={{a3}}={{a4}}  {{b0}}+{{b1}}+{{b2}}  {{c0}}x{{c1}}");
  for (int i = 0; i < 1000; ++i) {
    (void)injamm::render<kTmpl10>(d10);
    (void)std::format("{}={}={}={}={}  {}+{}+{}  {}x{}", d10.a0, d10.a1, d10.a2, d10.a3, d10.a4, d10.b0, d10.b1, d10.b2, d10.c0, d10.c1);
  }
  constexpr int ITERS10 = 200000;
  double        nt10    = bench("injamm NTTP render<kTmpl10>", ITERS10, [&] { return injamm::render<kTmpl10>(d10); });
  double        fmt10   = bench("std::format", ITERS10, [&] { return std::format("{}={}={}={}={}  {}+{}+{}  {}x{}", d10.a0, d10.a1, d10.a2, d10.a3, d10.a4, d10.b0, d10.b1, d10.b2, d10.c0, d10.c1); });

#ifdef INJAMM_BENCH_FMT
  // ---- fmt::format with FMT_COMPILE (compile-time parsed format string) ----
  std::printf("\n--- fmt::format (FMT_COMPILE: compile-time parse) ---\n");
  constexpr int ITERSF = 200000;
  double        f1     = bench("fmt FMT_COMPILE 1 var", ITERSF, [&] { return fmt::format(FMT_COMPILE("{}"), d1.val); });
  double        f2     = bench("fmt FMT_COMPILE 2 vars", ITERSF, [&] { return fmt::format(FMT_COMPILE("test example, {} = {}"), d2si.aaa, d2si.bbb); });
  double        f3     = bench("fmt FMT_COMPILE 3 vars", ITERSF, [&] { return fmt::format(FMT_COMPILE("test example, {} = {} on {}"), d3.aaa, d3.bbb, d3.ccc); });
  double        f10    = bench("fmt FMT_COMPILE 10 vars", ITERSF,
                               [&] { return fmt::format(FMT_COMPILE("{}={}={}={}={}  {}+{}+{}  {}x{}"), d10.a0, d10.a1, d10.a2, d10.a3, d10.a4, d10.b0, d10.b1, d10.b2, d10.c0, d10.c1); });
#endif

  // ---- escaped vs raw (3 vars, string contains HTML special chars) ----
  std::printf("\n--- escaped vs raw (3 vars, HTML chars) ---\n");
  Data3 const dhtml{"a<b>&\"c\"'d'", 42, 9.9};
  auto constexpr kTmplEsc  = injamm::fixed_string("{{aaa}} = {{bbb}} on {{ccc}}");
  auto constexpr kTmplRaw  = injamm::fixed_string("{{{aaa}}} = {{bbb}} on {{ccc}}");
  auto constexpr kTmpl3raw = injamm::fixed_string("test example, {{{aaa}}} = {{bbb}} on {{ccc}}");
  for (int i = 0; i < 1000; ++i) {
    (void)injamm::render<kTmplEsc>(dhtml);
    (void)injamm::render<kTmplRaw>(dhtml);
    (void)injamm::render<kTmpl3raw>(dhtml);
    (void)std::format("test example, {} = {} on {}", dhtml.aaa, dhtml.bbb, dhtml.ccc);
  }
  constexpr int ITERSE = 200000;
  double        esc    = bench("injamm NTTP {{aaa}} (escaped)", ITERSE, [&] { return injamm::render<kTmplEsc>(dhtml); });
  double        raw    = bench("injamm NTTP {{{aaa}}} (raw)", ITERSE, [&] { return injamm::render<kTmplRaw>(dhtml); });
  double        raw3   = bench("injamm NTTP {{{...}}} 3 vars (raw)", ITERSE, [&] { return injamm::render<kTmpl3raw>(dhtml); });
  double        fmt3e  = bench("std::format (no escape)", ITERSE, [&] { return std::format("test example, {} = {} on {}", dhtml.aaa, dhtml.bbb, dhtml.ccc); });

  // ---- runtime engine (constructed once, fresh string) ----
  std::printf("\n--- runtime engine (engine<T>, constructed once, fresh string) ---\n");
  injamm::engine<Data3> eng3("test example, {{aaa}} = {{bbb}} on {{ccc}}");
  for (int i = 0; i < 1000; ++i) {
    (void)eng3.render(d3);
    (void)std::format("test example, {} = {} on {}", d3.aaa, d3.bbb, d3.ccc);
  }
  constexpr int ITERSE3 = 200000;
  double        eng     = bench("injamm engine render (fresh)", ITERSE3, [&] { return eng3.render(d3); });
  double        fmt3r   = bench("std::format", ITERSE3, [&] { return std::format("test example, {} = {} on {}", d3.aaa, d3.bbb, d3.ccc); });

  // ---- buffer reuse (3 vars) ----
  std::printf("\n--- buffer reuse (3 vars) ---\n");
  auto constexpr kTmpl3nt = injamm::fixed_string("test example, {{aaa}} = {{bbb}} on {{ccc}}");
  std::string reused;
  reused.reserve(128);
  std::string fbuf;
  fbuf.reserve(128);
  for (int i = 0; i < 1000; ++i) {
    (void)eng3.render(d3, reused);
    (void)injamm::render<kTmpl3nt>(d3, reused);
    fbuf.clear();
    std::format_to(std::back_inserter(fbuf), "test example, {} = {} on {}", d3.aaa, d3.bbb, d3.ccc);
  }
  constexpr int ITERSBR = 200000;
  double        engr    = bench("engine render reuse buffer", ITERSBR, [&] {
    (void)eng3.render(d3, reused);
    return 0;
  });
  double        nttr    = bench("NTTP render reuse buffer", ITERSBR, [&] {
    (void)injamm::render<kTmpl3nt>(d3, reused);
    return 0;
  });
  double        f2t     = bench("std::format_to reuse buffer", ITERSBR, [&] {
    fbuf.clear();
    return std::format_to(std::back_inserter(fbuf), "test example, {} = {} on {}", d3.aaa, d3.bbb, d3.ccc);
  });

  // ---- mixed template: straight-line prefix/suffix + a section (hybrid path) ----
  //  ハイブリッドは直線区間（"catalog: " / " end."）をコンパイル時アンロールし、
  //  {{#items}}...{{/items}} 本体はランタイム VM に委譲する。
  std::printf("\n--- mixed template: prefix/suffix (unrolled) + section (VM-delegated) ---\n");
  auto constexpr kTmplMixed =
      injamm::fixed_string("{{title}} catalog: {{#items}}{{name}}={{price}}, {{/items}} end.");
  auto constexpr kTmplMixedRaw =
      injamm::fixed_string("{{title}} catalog: {{#items}}{{{name}}}={{{price}}}, {{/items}} end.");
  BenchCatalog bcat{"shop", {{"apple", 10}, {"banana", 5}, {"cherry", 8}}};
  injamm::engine<BenchCatalog> engMixed("{{title}} catalog: {{#items}}{{name}}={{price}}, {{/items}} end.");
  for (int i = 0; i < 1000; ++i) {
    (void)injamm::render<kTmplMixed>(bcat);
    (void)engMixed.render(bcat);
  }
  constexpr int ITERSMIX = 100000;
  double        ntMix     = bench("injamm NTTP hybrid render", ITERSMIX, [&] { return injamm::render<kTmplMixed>(bcat); });
  double        engMix    = bench("injamm engine render (VM)", ITERSMIX, [&] { return engMixed.render(bcat); });
  double        ntMixRaw  = bench("injamm NTTP hybrid (raw)", ITERSMIX, [&] { return injamm::render<kTmplMixedRaw>(bcat); });
  (void)ntMixRaw;

  // ---- 直線 only partial（htmx の行更新など）: コンパイル時アンロール vs engine VM ----
  //  render_partial<Tmpl, "row"> は直線 only の partial 本文をコンパイル時にアンロールする。
  //  対して engine.render(item, "row") は partial 本文を実行時コンパイルして VM 実行する。
  std::printf("\n--- straight-line partial (htmx row update): compile-time unroll vs VM ---\n");
  auto constexpr kTmplRow = injamm::fixed_string(
      "{{#partialdef row}}<tr><td>{{name}}</td><td>{{price}}</td></tr>{{/partialdef}}{{#items}}{{> row}}{{/items}}");
  BenchItem           bitem{"apple", 10};
  injamm::engine<BenchItem> engRow(
      "{{#partialdef row}}<tr><td>{{name}}</td><td>{{price}}</td></tr>{{/partialdef}}{{#items}}{{> row}}{{/items}}");
  for (int i = 0; i < 1000; ++i) {
    (void)injamm::render_partial<kTmplRow, injamm::fixed_string{"row"}>(bitem);
    (void)engRow.render(bitem, "row");
  }
  constexpr int ITERSROW = 300000;
  double        ntRow     = bench("injamm NTTP partial (unroll)", ITERSROW, [&] { return injamm::render_partial<kTmplRow, injamm::fixed_string{"row"}>(bitem); });
  double        engRowV   = bench("injamm engine partial (VM)", ITERSROW, [&] { return engRow.render(bitem, "row"); });

  // ---- summary ratio table (ratio = std::format ns / injamm ns) ----
  std::printf("\n=== summary: ratio = std::format ns / injamm ns (1.0 = same, <1 format faster, >1 injamm faster) ===\n");
  ratio_row("1 var (string)", fmt1, nt1);
  ratio_row("1 var (int)", fmt1i, nt1i);
  ratio_row("1 var (double)", fmt1d, nt1d);
  ratio_row("2 vars (str/int)", fmt2si, nt2si);
  ratio_row("2 vars (str/dbl)", fmt2sd, nt2sd);
  ratio_row("2 vars (int/dbl)", fmt2id, nt2id);
  ratio_row("3 vars (str/int/dbl)", fmt3, nt3);
  ratio_row("10 vars (5str/3int/2dbl)", fmt10, nt10);
  ratio_row("3 vars escaped  {{...}}", fmt3e, esc);
  ratio_row("3 vars raw      {{{...}}}", fmt3e, raw3);
  ratio_row("3 vars engine (fresh)", fmt3r, eng);
  ratio_row("3 vars engine reuse buf", f2t, engr);
  ratio_row("3 vars NTTP reuse buf", f2t, nttr);
#ifdef INJAMM_BENCH_FMT
  std::printf("--- fmt::format (FMT_COMPILE) vs injamm ---\n");
  ratio_row("1 var (string) fmt", f1, nt1);
  ratio_row("2 vars (str/int) fmt", f2, nt2si);
  ratio_row("3 vars (str/int/dbl) fmt", f3, nt3);
  ratio_row("10 vars fmt", f10, nt10);
#endif

  std::printf("\n=== done ===\n");
  return 0;
}
