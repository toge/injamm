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

static double elapsed_us(auto const& start, auto const& end) {
  return std::chrono::duration<double, std::micro>(end - start).count();
}

static int bench_filter_dispatch();

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

  std::printf("\n--- buffer reuse ---\n");
  bench_buffer_reuse();

  std::printf("\n--- section loop ---\n");
  bench_section_loop();

  std::printf("\n--- filter dispatch micro-benchmark ---\n");
  bench_filter_dispatch();

  std::printf("\n--- CT render ---\n");
  bench_ct_render();

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

