/**
 * @file test_codegen_extended.cpp
 * @brief injamm_codegen の拡張テスト
 */

#include <iostream>
#include <string>
#include <vector>

#include <injamm.hpp>
#include <injamm/escape_hatch.hpp>

struct User {
  std::string name;
  int age = 0;
  bool active = false;
  double score = 0.0;
};

template <>
struct glz::meta<User> {
  static constexpr auto value = glz::object(
    "name", &User::name,
    "age", &User::age,
    "active", &User::active,
    "score", &User::score
  );
};

#include "render_ext1.hpp"
#include "render_ext2.hpp"
#include "render_ext3.hpp"
#include "render_ext4.hpp"
#include "render_ext5.hpp"

int test_count = 0;
int pass_count = 0;

void check(std::string_view name, std::string_view tmpl_str, auto const& data, auto gen_func) {
  ++test_count;
  injamm::engine<User> eng{std::string(tmpl_str)};
  auto expected = eng.render(data);
  if (!expected) {
    std::cerr << "FAIL [" << name << "] runtime: " << expected.error().custom_error_message << "\n";
    return;
  }
  auto result = gen_func(data);
  if (!result) {
    std::cerr << "FAIL [" << name << "] codegen failed\n";
    return;
  }
  if (*expected == *result) {
    std::cout << "PASS [" << name << "]\n";
    ++pass_count;
  } else {
    std::cerr << "FAIL [" << name << "]\n";
    std::cerr << "  expected (" << expected->size() << "): [" << *expected << "]\n";
    std::cerr << "  got      (" << result->size() << "): [" << *result << "]\n";
  }
}

int main() {
  User u{"Alice", 30, true, 95.5};

  std::cout << "=== codegen 拡張テスト ===\n\n";

  check("num compare gt", "{{#if age > 20}}adult{{else}}minor{{/if}}", u,
    [](auto const& d) { return generated::render_ext1(d); });

  check("num compare eq", "{{#if age == 30}}match{{else}}no match{{/if}}", u,
    [](auto const& d) { return generated::render_ext2(d); });

  check("bool truthy", "{{#if active}}yes{{else}}no{{/if}}", u,
    [](auto const& d) { return generated::render_ext3(d); });

  check("raw output", "{{{name}}}", u,
    [](auto const& d) { return generated::render_ext4(d); });

  check("filter chain", "{{name|upper|lower}}", u,
    [](auto const& d) { return generated::render_ext5(d); });

  std::cout << "\n=== 結果: " << pass_count << "/" << test_count << " passed ===\n";
  return (pass_count == test_count) ? 0 : 1;
}
