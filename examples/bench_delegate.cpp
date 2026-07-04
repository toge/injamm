#include "injamm/escape_hatch.hpp"
#include <charconv>
#include <chrono>
#include <cstdio>
#include <format>
#include <string>

// ---- データ型 ----

struct Data3 {
  std::string aaa;
  int         bbb;
  double      ccc;
};

template <>
struct glz::meta<Data3> {
  using T = Data3;
  static constexpr auto value = object(&T::aaa, &T::bbb, &T::ccc);
};

struct Data1 {
  std::string val;
};

template <>
struct glz::meta<Data1> {
  using T = Data1;
  static constexpr auto value = object(&T::val);
};

struct Data10 {
  std::string a0, a1, a2, a3, a4;
  int         b0, b1, b2;
  double      c0, c1;
};

template <>
struct glz::meta<Data10> {
  using T = Data10;
  static constexpr auto value = object(
    &T::a0, &T::a1, &T::a2, &T::a3, &T::a4,
    &T::b0, &T::b1, &T::b2,
    &T::c0, &T::c1);
};

// ---- 手動直列構築 (delegate の理想上限) ----

static std::string manual3(Data3 const& d) {
  char buf[64];
  std::string out;
  out.reserve(64);
  out += "test example, ";
  out += d.aaa;
  out += " = ";
  auto [p, ec] = std::to_chars(buf, buf + 64, d.bbb);
  out.append(buf, p - buf);
  out += " on ";
  auto [p2, ec2] = std::to_chars(buf, buf + 64, d.ccc);
  out.append(buf, p2 - buf);
  return out;
}

static std::string manual1(Data1 const& d) {
  std::string out;
  out.reserve(32);
  out += d.val;
  return out;
}

static std::string manual10(Data10 const& d) {
  char buf[64];
  std::string out;
  out.reserve(128);
  auto cat_int = [&](int v) {
    auto [p, _] = std::to_chars(buf, buf + 64, v);
    out.append(buf, p - buf);
  };
  auto cat_dbl = [&](double v) {
    auto [p, _] = std::to_chars(buf, buf + 64, v);
    out.append(buf, p - buf);
  };
  out += d.a0; out += '=';
  out += d.a1; out += '=';
  out += d.a2; out += '=';
  out += d.a3; out += '=';
  out += d.a4; out += "  ";
  cat_int(d.b0); out += '+';
  cat_int(d.b1); out += '+';
  cat_int(d.b2); out += "  ";
  cat_dbl(d.c0); out += 'x';
  cat_dbl(d.c1);
  return out;
}

// ---- ヘルパー ----

static double elapsed_us(auto const& start, auto const& end) {
  return std::chrono::duration<double, std::micro>(end - start).count();
}

template <typename F>
static void bench(char const* label, int iters, F&& f) {
  auto start = std::chrono::high_resolution_clock::now();
  for (int i = 0; i < iters; ++i)
    (void)f();
  auto end = std::chrono::high_resolution_clock::now();
  auto us = elapsed_us(start, end);
  std::printf("  %-36s x %6d: %8.0f us  (%6.1f ns/call)\n", label, iters, us, us * 1000.0 / iters);
}

int main() {
  std::printf("=== benchmark: VM vs direct string building (delegate ideal) ===\n\n");

  // ---- 1 variable ----
  std::printf("--- 1 var (string) ---\n");
  Data1 const d1{"hello world"};
  auto constexpr kTmpl1 = injamm::fixed_string("{{val}}");
  for (int i = 0; i < 1000; ++i) {
    (void)injamm::render<kTmpl1>(d1);
    (void)manual1(d1);
  }
  {
    constexpr int ITERS = 200000;
    bench("injamm render<kTmpl1>",        ITERS, [&] { return injamm::render<kTmpl1>(d1); });
    bench("manual string building",       ITERS, [&] { return manual1(d1); });
    bench("std::format",                  ITERS, [&] { return std::format("{}", d1.val); });
  }

  // ---- 3 variables: string, int, double ----
  std::printf("\n--- 3 vars (string, int, double) ---\n");
  Data3 const d3{"5", 503, 35.3};
  auto constexpr kTmpl3 = injamm::fixed_string("test example, {{aaa}} = {{bbb}} on {{ccc}}");
  for (int i = 0; i < 1000; ++i) {
    (void)injamm::render<kTmpl3>(d3);
    (void)manual3(d3);
  }
  {
    constexpr int ITERS = 200000;
    bench("injamm render<kTmpl3>",        ITERS, [&] { return injamm::render<kTmpl3>(d3); });
    bench("manual string building",       ITERS, [&] { return manual3(d3); });
    bench("std::format (typed)",          ITERS, [&] { return std::format("test example, {} = {} on {}", d3.aaa, d3.bbb, d3.ccc); });
  }

  // ---- 10 variables (mixed types) ----
  std::printf("\n--- 10 vars (5 string + 3 int + 2 double) ---\n");
  Data10 d10;
  d10.a0 = "xyz"; d10.a1 = "abc"; d10.a2 = "def"; d10.a3 = "ghi"; d10.a4 = "jkl";
  d10.b0 = 10; d10.b1 = 20; d10.b2 = 30;
  d10.c0 = 1.5; d10.c1 = 2.5;
  auto constexpr kTmpl10 = injamm::fixed_string("{{a0}}={{a1}}={{a2}}={{a3}}={{a4}}  {{b0}}+{{b1}}+{{b2}}  {{c0}}x{{c1}}");
  for (int i = 0; i < 1000; ++i) {
    (void)injamm::render<kTmpl10>(d10);
    (void)manual10(d10);
  }
  {
    constexpr int ITERS = 200000;
    bench("injamm render<kTmpl10>",       ITERS, [&] { return injamm::render<kTmpl10>(d10); });
    bench("manual string building",       ITERS, [&] { return manual10(d10); });
    bench("std::format",                  ITERS, [&] { return std::format("{}={}={}={}={}  {}+{}+{}  {}x{}",
      d10.a0, d10.a1, d10.a2, d10.a3, d10.a4,
      d10.b0, d10.b1, d10.b2, d10.c0, d10.c1); });
  }

  // ---- buffer reuse comparison (3 vars) ----
  std::printf("\n--- buffer reuse comparison (3 vars) ---\n");
  injamm::engine<Data3> eng3("test example, {{aaa}} = {{bbb}} on {{ccc}}");
  auto constexpr kTmpl3_nttp = injamm::fixed_string("test example, {{aaa}} = {{bbb}} on {{ccc}}");
  std::string reused;
  reused.reserve(128);
  for (int i = 0; i < 1000; ++i) {
    (void)eng3.render(d3);
    (void)eng3.render(d3, reused);
    (void)injamm::render<kTmpl3_nttp>(d3);
    (void)injamm::render<kTmpl3_nttp>(d3, reused);
  }
  {
    constexpr int ITERS = 100000;
    bench("engine render new string",         ITERS, [&] { return eng3.render(d3); });
    bench("engine render reuse buffer",       ITERS, [&] { (void)eng3.render(d3, reused); return 0; });
    bench("NTTP render new string",           ITERS, [&] { return injamm::render<kTmpl3_nttp>(d3); });
    bench("NTTP render reuse buffer",         ITERS, [&] { (void)injamm::render<kTmpl3_nttp>(d3, reused); return 0; });
    bench("manual string building",           ITERS, [&] { return manual3(d3); });
  }

  // ---- buffer reuse with user pre-reserve ----
  std::printf("\n--- buffer reuse with large reserve (3 vars) ---\n");
  std::string big_buf;
  big_buf.reserve(4096);
  for (int i = 0; i < 1000; ++i) {
    (void)eng3.render(d3, big_buf);
    (void)injamm::render<kTmpl3_nttp>(d3, big_buf);
  }
  {
    constexpr int ITERS = 200000;
    bench("engine render reuse(4096)",        ITERS, [&] { (void)eng3.render(d3, big_buf); return 0; });
    bench("NTTP render reuse(4096)",          ITERS, [&] { (void)injamm::render<kTmpl3_nttp>(d3, big_buf); return 0; });
    bench("manual + reserve(64)",             ITERS, [&] { return manual3(d3); });
  }

  // ---- large output: many vars with buffer reuse ----
  std::printf("\n--- large output (10 vars) buffer reuse ---\n");
  std::string big_reused;
  big_reused.reserve(1024);
  auto constexpr kTmpl10_nttp = injamm::fixed_string("{{a0}}={{a1}}={{a2}}={{a3}}={{a4}}  {{b0}}+{{b1}}+{{b2}}  {{c0}}x{{c1}}");
  for (int i = 0; i < 1000; ++i) {
    (void)injamm::render<kTmpl10_nttp>(d10);
    (void)injamm::render<kTmpl10_nttp>(d10, big_reused);
    (void)manual10(d10);
  }
  {
    constexpr int ITERS = 200000;
    bench("NTTP render new string",           ITERS, [&] { return injamm::render<kTmpl10_nttp>(d10); });
    bench("NTTP render reuse(1K)",            ITERS, [&] { (void)injamm::render<kTmpl10_nttp>(d10, big_reused); return 0; });
    bench("manual string building",           ITERS, [&] { return manual10(d10); });
  }

  std::printf("\n=== done ===\n");
  return 0;
}
