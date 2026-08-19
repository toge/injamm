/**
 * @file test_codegen_extended.cpp
 * @brief injamm_codegen の拡張テスト
 */

#include <iostream>
#include <string>
#include <vector>

#include <injamm.hpp>
#include <injamm/escape_hatch.hpp>
#include "codegen_helpers.hpp"

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
#include "render_drift_hex.hpp"
#include "render_drift_fcmp.hpp"
#include "render_drift_fadd.hpp"
#include "render_drift_repl.hpp"
#include "render_drift_pad.hpp"
#include "render_drift_sub1.hpp"
#include "render_drift_sub2.hpp"

// 新機能テスト用テンプレート
struct Item2 {
  std::string name;
  int quantity = 0;
};
struct TestData {
  std::string name;
  bool active = false;
  int age = 0;
  std::vector<Item2> items;
  std::vector<int> nums;
};

template <>
struct glz::meta<Item2> {
  static constexpr auto value = glz::object("name", &Item2::name, "quantity", &Item2::quantity);
};
template <>
struct glz::meta<TestData> {
  static constexpr auto value = glz::object("name", &TestData::name, "active", &TestData::active,
                                             "age", &TestData::age, "items", &TestData::items,
                                             "nums", &TestData::nums);
};

#include "render6.hpp"
#include "render7.hpp"
#include "render8.hpp"
#include "render9.hpp"
#include "render10.hpp"
#include "render11.hpp"
#include "render12.hpp"
#include "render13.hpp"

int test_count = 0;
int pass_count = 0;

void check_into(std::string_view name, std::string_view tmpl_str, auto const& data, auto gen_func) {
  ++test_count;
  injamm::engine<std::decay_t<decltype(data)>> eng{std::string(tmpl_str)};
  auto expected = eng.render(data);
  if (!expected) {
    std::cerr << "FAIL [" << name << "] runtime: " << expected.error().custom_error_message << "\n";
    return;
  }
  std::string out;
  out = "garbage";
  auto result = gen_func(data, out);
  if (!result) {
    std::cerr << "FAIL [" << name << "] codegen_into failed\n";
    return;
  }
  if (*expected == out) {
    std::cout << "PASS [" << name << "]\n";
    ++pass_count;
  } else {
    std::cerr << "FAIL [" << name << "]\n";
    std::cerr << "  expected (" << expected->size() << "): [" << *expected << "]\n";
    std::cerr << "  got      (" << out.size() << "): [" << out << "]\n";
  }
}

void check_ext(std::string_view name, std::string_view tmpl_str, auto const& data, auto gen_func) {
  ++test_count;
  injamm::engine<std::decay_t<decltype(data)>> eng{std::string(tmpl_str)};
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
  TestData d{"MyApp", true, 25, {{"Widget", 3}, {"Gadget", 7}}, {10, 20, 30}};

  std::cout << "=== codegen 拡張テスト ===\n\n";

  check_ext("num compare gt", "{{#if age > 20}}adult{{else}}minor{{/if}}", u,
    [](auto const& d) { return generated::render_ext1(d); });

  check_ext("num compare eq", "{{#if age == 30}}match{{else}}no match{{/if}}", u,
    [](auto const& d) { return generated::render_ext2(d); });

  check_ext("bool truthy", "{{#if active}}yes{{else}}no{{/if}}", u,
    [](auto const& d) { return generated::render_ext3(d); });

  check_ext("raw output", "{{{name}}}", u,
    [](auto const& d) { return generated::render_ext4(d); });

  check_ext("filter chain", "{{name|upper|lower}}", u,
    [](auto const& d) { return generated::render_ext5(d); });

  // 新機能テスト: emit_this / emit_at_root / logical / filters
  // 各コード生成済み render 関数はそれぞれ特定のテンプレートから生成されている
  check_ext("this aka {{.}}", "Hello {{this}} World", std::string{"hello"},
    [](auto const& d) { return generated::render6(d); });
  check_ext("dot in section", "{{#nums}}[{{.}}]{{/nums}}", d,
    [](auto const& d) { return generated::render7(d); });
  check_ext("root in section", "{{#items}}{{root.name}}:{{name}}{{/items}}", d,
    [](auto const& d) { return generated::render8(d); });
  check_ext("not operator", "{{#if !active}}inactive{{/if}}", d,
    [](auto const& d) { return generated::render9(d); });
  check_ext("first section", "{{#items}}[{{#loop.is_first}}F:{{name}}{{/loop.is_first}}][{{^loop.is_first}}N:{{name}}{{/loop.is_first}}]{{/items}}", d,
    [](auto const& d) { return generated::render11(d); });
  check_ext("last section", "{{#items}}[{{#loop.is_last}}L:{{name}}{{/loop.is_last}}]{{/items}}", d,
    [](auto const& d) { return generated::render12(d); });
  check_ext("json filter", "{{name|json}}", d,
    [](auto const& d) { return generated::render13(d); });

  // バッファ再利用版
  check_into("into num compare gt", "{{#if age > 20}}adult{{else}}minor{{/if}}", u,
    [](auto const& d, std::string& out) { return generated::render_ext1(d, out); });
  check_into("into raw", "{{{name}}}", u,
    [](auto const& d, std::string& out) { return generated::render_ext4(d, out); });
  check_into("into dot section", "{{#nums}}[{{.}}]{{/nums}}", d,
    [](auto const& d, std::string& out) { return generated::render7(d, out); });
  check_into("into not", "{{#if !active}}inactive{{/if}}", d,
    [](auto const& d, std::string& out) { return generated::render9(d, out); });
  check_into("into json", "{{name|json}}", d,
    [](auto const& d, std::string& out) { return generated::render13(d, out); });

  // ドリフト回帰テスト: codegen_helpers.hpp と filters.hpp の実装差分を防ぐ
  // 各テストは VM (injamm::engine) と codegen (生成コード) の出力が一致することを確認
  std::cout << "\n--- drift regression ---\n";
  check_ext("drift hex", "{{age | hex}}", u,
    [](auto const& d) { return generated::render_drift_hex(d); });
  check_ext("drift float gt", "{{score | gt(90)}}", u,
    [](auto const& d) { return generated::render_drift_fcmp(d); });
  check_ext("drift float add", "{{score | add(1)}}", u,
    [](auto const& d) { return generated::render_drift_fadd(d); });
  check_ext("drift replace no-arg", "{{name | replace(\"\", \"X\")}}", u,
    [](auto const& d) { return generated::render_drift_repl(d); });
  check_ext("drift pad empty str", "{{name | pad(10, \"\")}}", u,
    [](auto const& d) { return generated::render_drift_pad(d); });
  check_ext("drift substr neg start", "{{name | substr(-1, 3)}}", u,
    [](auto const& d) { return generated::render_drift_sub1(d); });
  check_ext("drift substr len 0", "{{name | substr(1, 0)}}", u,
    [](auto const& d) { return generated::render_drift_sub2(d); });

  std::cout << "\n=== 結果: " << pass_count << "/" << test_count << " passed ===\n";
  return (pass_count == test_count) ? 0 : 1;
}
