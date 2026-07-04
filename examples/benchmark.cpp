#include "injamm/escape_hatch.hpp"
#include <chrono>
#include <cstdio>
#include <numeric>
#include <string>
#include <vector>

struct Person {
  std::string name;
  int age;
};

template <>
struct glz::meta<Person> {
  using T = Person;
  static constexpr auto value = object(&T::name, &T::age);
};

struct Team {
  std::string name;
  std::vector<Person> members;
};

template <>
struct glz::meta<Team> {
  using T = Team;
  static constexpr auto value = object(&T::name, &T::members);
};

struct TeamLarge {
  std::string name;
  std::vector<Person> members;
};

template <>
struct glz::meta<TeamLarge> {
  using T = TeamLarge;
  static constexpr auto value = object(&T::name, &T::members);
};

/// P3 確認用: 20フィールド構造体
struct WideStruct {
  std::string f0, f1, f2, f3, f4, f5, f6, f7, f8, f9;
  std::string f10, f11, f12, f13, f14, f15, f16, f17, f18, f19;
};

template <>
struct glz::meta<WideStruct> {
  using T = WideStruct;
  static constexpr auto value = object(
    &T::f0, &T::f1, &T::f2, &T::f3, &T::f4,
    &T::f5, &T::f6, &T::f7, &T::f8, &T::f9,
    &T::f10, &T::f11, &T::f12, &T::f13, &T::f14,
    &T::f15, &T::f16, &T::f17, &T::f18, &T::f19);
};

/// P4 enum 解決ベンチマーク用
enum class Color : int { Red = 1, Green = 2, Blue = 3 };

struct EnumItem {
  std::string name;
  Color       color;
};

template <>
struct glz::meta<EnumItem> {
  using T = EnumItem;
  static constexpr auto value = object("name", &EnumItem::name, "color", &EnumItem::color);
};

/// P4 SoA 用: 大量の命令列を生成する big テンプレート
struct BigData {
  std::string a0, a1, a2, a3, a4, a5, a6, a7, a8, a9;
};

template <>
struct glz::meta<BigData> {
  using T = BigData;
  static constexpr auto value = object(
    "a0", &T::a0, "a1", &T::a1, "a2", &T::a2, "a3", &T::a3, "a4", &T::a4,
    "a5", &T::a5, "a6", &T::a6, "a7", &T::a7, "a8", &T::a8, "a9", &T::a9);
};

static double elapsed_us(auto const& start, auto const& end) {
  return std::chrono::duration<double, std::micro>(end - start).count();
}

static int bench_filter_dispatch();
static int bench_wide_struct();
static int bench_bind_context();
static int bench_section_loop_large();
static int bench_enum_resolve();
static int bench_many_vars();

template <size_t N>
static std::string repeat(std::string_view s) {
  std::string r;
  r.reserve(s.size() * N);
  for (size_t i = 0; i < N; ++i)
    r += s;
  return r;
}

static int bench_filter_chain() {
  Person alice{"Alice", 30};
  auto tmpl = R"(Hello {{name|upper}}! You are {{name|trim|upper|truncate:10}}.)";

  injamm::engine<Person> eng(tmpl);

  // warmup
  for (int i = 0; i < 1000; ++i)
    (void)eng.render(alice);

  constexpr int ITERS = 50000;
  auto start = std::chrono::high_resolution_clock::now();
  for (int i = 0; i < ITERS; ++i)
    (void)eng.render(alice);
  auto end = std::chrono::high_resolution_clock::now();

  auto us = elapsed_us(start, end);
  std::printf("  filter_chain x %d: %.0f us  (%.1f ns/call)\n", ITERS, us, us * 1000.0 / ITERS);
  return 0;
}

static int bench_buffer_prealloc() {
  Person bob{"Bob", 25};
  auto large_literal = repeat<500>("Hello {{name}}, you are {{age}} years old. ");
  auto tmpl = large_literal + "{{name}}!";

  injamm::engine<Person> eng(tmpl);

  for (int i = 0; i < 100; ++i)
    (void)eng.render(bob);

  // Measure total allocation size by comparing string capacity
  auto r = eng.render(bob);
  (void)r;

  constexpr int ITERS = 5000;
  auto start = std::chrono::high_resolution_clock::now();
  for (int i = 0; i < ITERS; ++i)
    (void)eng.render(bob);
  auto end = std::chrono::high_resolution_clock::now();

  auto us = elapsed_us(start, end);
  std::printf("  buffer_prealloc (%%d vars) x %d: %.0f us  (%.1f ns/call)\n", ITERS, us, us * 1000.0 / ITERS);
  return 0;
}

static int bench_buffer_reuse() {
  Person charlie{"Charlie", 35};
  auto tmpl = R"(Name: {{name|upper}}, Age: {{age}})";

  injamm::engine<Person> eng(tmpl);

  for (int i = 0; i < 100; ++i)
    (void)eng.render(charlie);

  constexpr int ITERS = 50000;
  std::string out;

  // new string each time
  {
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITERS; ++i) {
      auto r = eng.render(charlie);
      (void)r;
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto us = elapsed_us(start, end);
    std::printf("  buffer_reuse new_string x %d: %.0f us  (%.1f ns/call)\n", ITERS, us, us * 1000.0 / ITERS);
  }

  // reuse buffer
  {
    std::string reused;
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITERS; ++i) {
      (void)eng.render(charlie, reused);
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto us = elapsed_us(start, end);
    std::printf("  buffer_reuse reuse_buffer x %d: %.0f us  (%.1f ns/call)\n", ITERS, us, us * 1000.0 / ITERS);
  }

  return 0;
}

static int bench_section_loop() {
  Team team{"Devs", {{"Alice", 30}, {"Bob", 25}, {"Charlie", 35}}};

  auto tmpl = "Team {{name}}: {{#members}}  - {{name}} ({{age}})\n{{/members}}";
  injamm::engine<Team> eng(tmpl);

  for (int i = 0; i < 1000; ++i)
    (void)eng.render(team);

  constexpr int ITERS = 20000;
  auto start = std::chrono::high_resolution_clock::now();
  for (int i = 0; i < ITERS; ++i)
    (void)eng.render(team);
  auto end = std::chrono::high_resolution_clock::now();

  auto us = elapsed_us(start, end);
  std::printf("  section_loop x %d: %.0f us  (%.1f ns/call)\n", ITERS, us, us * 1000.0 / ITERS);
  return 0;
}

static int bench_section_loop_large() {
  std::vector<Person> people;
  people.reserve(1000);
  for (int i = 0; i < 1000; ++i)
    people.push_back({"User" + std::to_string(i), i % 100});
  TeamLarge team{"BigTeam", std::move(people)};

  auto tmpl = "{{#members}}{{name}} ({{age}})\n{{/members}}";
  injamm::engine<TeamLarge> eng(tmpl);

  for (int i = 0; i < 100; ++i)
    (void)eng.render(team);

  constexpr int ITERS = 1000;
  auto start = std::chrono::high_resolution_clock::now();
  for (int i = 0; i < ITERS; ++i)
    (void)eng.render(team);
  auto end = std::chrono::high_resolution_clock::now();

  auto us = elapsed_us(start, end);
  std::printf("  section_loop_large(1000elem) x %d: %.0f us  (%.1f ns/elem)\n", ITERS, us, us * 1000.0 / ITERS / 1000);
  return 0;
}

static int bench_ct_render() {
  Person dave{"Dave", 40};

  auto constexpr kTmpl = injamm::fixed_string("Hello {{name|upper}}! Age: {{age}}");

  for (int i = 0; i < 1000; ++i)
    (void)injamm::render<kTmpl>(dave);

  constexpr int ITERS = 50000;
  auto start = std::chrono::high_resolution_clock::now();
  for (int i = 0; i < ITERS; ++i)
    (void)injamm::render<kTmpl>(dave);
  auto end = std::chrono::high_resolution_clock::now();

  auto us = elapsed_us(start, end);
  std::printf("  ct_render x %d: %.0f us  (%.1f ns/call)\n", ITERS, us, us * 1000.0 / ITERS);
  return 0;
}

int main() {
  std::printf("=== Benchmark ===\n\n");

  std::printf("--- filter chain ---\n");
  bench_filter_chain();

  std::printf("\n--- buffer pre-allocation ---\n");
  bench_buffer_prealloc();

  std::printf("\n--- enum resolution ---\n");
  bench_enum_resolve();

  std::printf("\n--- many vars (SoA cache) ---\n");
  bench_many_vars();

  std::printf("\n--- buffer reuse ---\n");
  bench_buffer_reuse();

  std::printf("\n--- section loop ---\n");
  bench_section_loop();
  bench_section_loop_large();

  std::printf("\n--- filter dispatch micro-benchmark ---\n");
  bench_filter_dispatch();

  std::printf("\n--- CT render ---\n");
  bench_ct_render();

  std::printf("\n--- wide struct (P3 field-index dispatch) ---\n");
  bench_wide_struct();

  std::printf("\n--- bind context (コンテナ直接バインド) ---\n");
  bench_bind_context();

  std::printf("\n=== done ===\n");
  return 0;
}

static int bench_filter_dispatch() {
  Person alice{"Alice", 30};

  injamm::engine<Person> eng_no_filter("Hello {{name}}");
  injamm::engine<Person> eng_filter("Hello {{name|upper}}");

  for (int i = 0; i < 1000; ++i) {
    (void)eng_no_filter.render(alice);
    (void)eng_filter.render(alice);
  }

  constexpr int ITERS = 100000;

  // no filter
  {
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITERS; ++i)
      (void)eng_no_filter.render(alice);
    auto end = std::chrono::high_resolution_clock::now();
    auto us = elapsed_us(start, end);
    std::printf("  no_filter x %d: %.0f us  (%.1f ns/call)\n", ITERS, us, us * 1000.0 / ITERS);
  }

  // with filter
  {
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITERS; ++i)
      (void)eng_filter.render(alice);
    auto end = std::chrono::high_resolution_clock::now();
    auto us = elapsed_us(start, end);
    std::printf("  with_filter x %d: %.0f us  (%.1f ns/call)\n", ITERS, us, us * 1000.0 / ITERS);
  }

  // multi-filter chain
  injamm::engine<Person> eng_multi("Hello {{name|trim|upper|truncate:10}}");
  for (int i = 0; i < 1000; ++i)
    (void)eng_multi.render(alice);
  {
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITERS; ++i)
      (void)eng_multi.render(alice);
    auto end = std::chrono::high_resolution_clock::now();
    auto us = elapsed_us(start, end);
    std::printf("  multi_filter x %d: %.0f us  (%.1f ns/call)\n", ITERS, us, us * 1000.0 / ITERS);
  }

  return 0;
}

/**
 * @brief P3 確認用: 20フィールド構造体へのアクセスをベンチマーク
 * @details fold式 (||展開) でのフィールドインデックスディスパッチのコストを
 *          フィールド数が多い構造体で確認する。
 */
static int bench_wide_struct() {
  WideStruct ws;
  ws.f0 = "a"; ws.f5 = "b"; ws.f10 = "c"; ws.f15 = "d"; ws.f19 = "e";
  for (auto& s : {&ws.f1, &ws.f2, &ws.f3, &ws.f4, &ws.f6, &ws.f7, &ws.f8, &ws.f9,
                  &ws.f11, &ws.f12, &ws.f13, &ws.f14, &ws.f16, &ws.f17, &ws.f18}) {
    *s = "x";
  }

  // 先頭フィールド (index 0)、中間フィールド (index 10)、末尾フィールド (index 19) を対象にする
  injamm::engine<WideStruct> eng_first("{{f0}}");
  injamm::engine<WideStruct> eng_mid("{{f10}}");
  injamm::engine<WideStruct> eng_last("{{f19}}");
  injamm::engine<WideStruct> eng_all("{{f0}}{{f5}}{{f10}}{{f15}}{{f19}}");

  for (int i = 0; i < 1000; ++i) {
    (void)eng_first.render(ws);
    (void)eng_mid.render(ws);
    (void)eng_last.render(ws);
    (void)eng_all.render(ws);
  }

  constexpr int ITERS = 100000;
  {
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITERS; ++i)
      (void)eng_first.render(ws);
    auto end = std::chrono::high_resolution_clock::now();
    auto us = elapsed_us(start, end);
    std::printf("  wide_first(idx=0)  x %d: %.0f us  (%.1f ns/call)\n", ITERS, us, us * 1000.0 / ITERS);
  }
  {
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITERS; ++i)
      (void)eng_mid.render(ws);
    auto end = std::chrono::high_resolution_clock::now();
    auto us = elapsed_us(start, end);
    std::printf("  wide_mid(idx=10)   x %d: %.0f us  (%.1f ns/call)\n", ITERS, us, us * 1000.0 / ITERS);
  }
  {
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITERS; ++i)
      (void)eng_last.render(ws);
    auto end = std::chrono::high_resolution_clock::now();
    auto us = elapsed_us(start, end);
    std::printf("  wide_last(idx=19)  x %d: %.0f us  (%.1f ns/call)\n", ITERS, us, us * 1000.0 / ITERS);
  }
  {
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITERS; ++i)
      (void)eng_all.render(ws);
    auto end = std::chrono::high_resolution_clock::now();
    auto us = elapsed_us(start, end);
    std::printf("  wide_all(5 refs)   x %d: %.0f us  (%.1f ns/call)\n", ITERS, us, us * 1000.0 / ITERS);
  }
  return 0;
}

// ---- bind（コンテナ直接バインド）ベンチマーク ----

struct BenchUser {
  std::string name;
  int age{};
};

template <>
struct glz::meta<BenchUser> {
  static constexpr auto value = glz::object("name", &BenchUser::name, "age", &BenchUser::age);
};

static auto make_large_user_list(std::size_t n) {
  std::vector<BenchUser> users;
  users.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    users.push_back({.name = "User" + std::to_string(i), .age = static_cast<int>(i % 100)});
  }
  return users;
}

struct BenchUserList {
  std::vector<BenchUser> users;
};

template <>
struct glz::meta<BenchUserList> {
  static constexpr auto value = glz::object("users", &BenchUserList::users);
};

static int bench_bind_context() {
  auto constexpr kUserListTmpl = injamm::fixed_string("{{#users}}{{name}}:{{age}}\n{{/users}}");

  auto const users = make_large_user_list(1000);

  // struct context ウォームアップ
  BenchUserList ctx{users};
  for (int i = 0; i < 100; ++i) {
    (void)injamm::render<kUserListTmpl>(ctx);
  }

  // bind context ウォームアップ
  for (int i = 0; i < 100; ++i) {
    (void)injamm::render<kUserListTmpl>(injamm::bind<"users">(users));
  }

  constexpr int ITERS = 500;

  // struct context ベンチマーク
  auto start_s = std::chrono::high_resolution_clock::now();
  for (int i = 0; i < ITERS; ++i) {
    (void)injamm::render<kUserListTmpl>(ctx);
  }
  auto end_s = std::chrono::high_resolution_clock::now();
  auto us_s = elapsed_us(start_s, end_s);
  std::printf("  struct_context x %d: %.0f us  (%.1f us/call)\n", ITERS, us_s, us_s / ITERS);

  // bind context ベンチマーク
  auto start_b = std::chrono::high_resolution_clock::now();
  for (int i = 0; i < ITERS; ++i) {
    (void)injamm::render<kUserListTmpl>(injamm::bind<"users">(users));
  }
  auto end_b = std::chrono::high_resolution_clock::now();
  auto us_b = elapsed_us(start_b, end_b);
  std::printf("  bind_context   x %d: %.0f us  (%.1f us/call)\n", ITERS, us_b, us_b / ITERS);

  auto ratio = (us_s > 0.0) ? (us_b / us_s) : 0.0;
  std::printf("  ratio bind/struct: %.2f (±10%% target: 0.90 - 1.10)\n", ratio);
  return 0;
}

/**
 * @brief enum 値解決のベンチマーク
 * @details enchantum 経由の enum 名前解決コストを文字列出力と比較する。
 *          テンプレート内で enum フィールドを参照した場合のオーバーヘッドを測定。
 */
static int bench_enum_resolve() {
  EnumItem item{"Alice", Color::Red};

  // string フィールドのみ
  injamm::engine<EnumItem> eng_str("{{name}}");
  // enum フィールドのみ
  injamm::engine<EnumItem> eng_enum("{{color}}");
  // string + enum
  injamm::engine<EnumItem> eng_both("{{name}} {{color}}");

  for (int i = 0; i < 1000; ++i) {
    (void)eng_str.render(item);
    (void)eng_enum.render(item);
    (void)eng_both.render(item);
  }

  constexpr int ITERS = 100000;

  {
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITERS; ++i)
      (void)eng_str.render(item);
    auto end = std::chrono::high_resolution_clock::now();
    auto us = elapsed_us(start, end);
    std::printf("  enum_str_field   x %d: %.0f us  (%.1f ns/call)\n", ITERS, us, us * 1000.0 / ITERS);
  }
  {
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITERS; ++i)
      (void)eng_enum.render(item);
    auto end = std::chrono::high_resolution_clock::now();
    auto us = elapsed_us(start, end);
    std::printf("  enum_field       x %d: %.0f us  (%.1f ns/call)\n", ITERS, us, us * 1000.0 / ITERS);
  }
  {
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITERS; ++i)
      (void)eng_both.render(item);
    auto end = std::chrono::high_resolution_clock::now();
    auto us = elapsed_us(start, end);
    std::printf("  enum_str_both    x %d: %.0f us  (%.1f ns/call)\n", ITERS, us, us * 1000.0 / ITERS);
  }

  // 列挙子名が長い場合の enum 解決
  EnumItem item2{"Bob", Color::Blue};
  injamm::engine<EnumItem> eng_long_color("{{name}} {{color}}");
  for (int i = 0; i < 1000; ++i)
    (void)eng_long_color.render(item2);

  {
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITERS; ++i)
      (void)eng_long_color.render(item2);
    auto end = std::chrono::high_resolution_clock::now();
    auto us = elapsed_us(start, end);
    std::printf("  enum_long_color  x %d: %.0f us  (%.1f ns/call)\n", ITERS, us, us * 1000.0 / ITERS);
  }

  // 出力バッファ再利用時の差分
  constexpr int ITERS2 = 50000;
  std::string reused;
  {
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITERS2; ++i)
      (void)eng_enum.render(item, reused);
    auto end = std::chrono::high_resolution_clock::now();
    auto us = elapsed_us(start, end);
    std::printf("  enum_reuse_buf   x %d: %.0f us  (%.1f ns/call)\n", ITERS2, us, us * 1000.0 / ITERS2);
  }
  return 0;
}

/**
 * @brief 多変数テンプレートのベンチマーク
 * @details 多数の変数参照を含むテンプレートをレンダリングし、
 *          bc_var_ref AoS レイアウトのキャッシュ振る舞いを評価する。
 *          命令数・var_ref 数が多い場合の性能を測定。
 */
static int bench_many_vars() {
  BigData data{"v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v8", "v9"};

  // 10個の変数を多数回参照するテンプレート
  auto tmpl = "{{a0}}{{a1}}{{a2}}{{a3}}{{a4}}{{a5}}{{a6}}{{a7}}{{a8}}{{a9}}";
  auto large = repeat<100>(tmpl) + "{{a0}}";

  injamm::engine<BigData> eng(large);

  for (int i = 0; i < 100; ++i)
    (void)eng.render(data);

  constexpr int ITERS = 5000;
  auto start = std::chrono::high_resolution_clock::now();
  for (int i = 0; i < ITERS; ++i)
    (void)eng.render(data);
  auto end = std::chrono::high_resolution_clock::now();
  auto us = elapsed_us(start, end);
  std::printf("  many_vars(%zu refs) x %d: %.0f us  (%.1f ns/call)\n",
              size_t{100} * 10 + 1, ITERS, us, us * 1000.0 / ITERS);

  // 命令数が多いテンプレート（長いリテラル・多数の命令）
  auto long_template = repeat<300>("Hello {{a0}} value {{a1}} end. ");
  injamm::engine<BigData> eng_long(long_template);
  for (int i = 0; i < 100; ++i)
    (void)eng_long.render(data);

  {
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITERS; ++i)
      (void)eng_long.render(data);
    auto end = std::chrono::high_resolution_clock::now();
    auto us = elapsed_us(start, end);
    std::printf("  long_literals(300 blk) x %d: %.0f us  (%.1f ns/call)\n", ITERS, us, us * 1000.0 / ITERS);
  }

  // 同じテンプレートをバッファ再利用で
  {
    std::string reused;
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITERS; ++i)
      (void)eng_long.render(data, reused);
    auto end = std::chrono::high_resolution_clock::now();
    auto us = elapsed_us(start, end);
    std::printf("  long_literals(reuse)  x %d: %.0f us  (%.1f ns/call)\n", ITERS, us, us * 1000.0 / ITERS);
  }
  return 0;
}
