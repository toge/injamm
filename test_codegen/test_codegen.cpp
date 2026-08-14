/**
 * @file test_codegen.cpp
 * @brief injamm_codegen で生成されたコードの動作テスト
 *
 * @details injamm のランタイムレンダリングと生成コードの結果を比較する
 */

#include <iostream>
#include <string>
#include <vector>

#include <injamm.hpp>
#include <injamm/escape_hatch.hpp>
#include "codegen_helpers.hpp"

// ============================================================
// テストデータ型
// ============================================================

struct ItemData {
  std::string name;
  int quantity = 0;
  double price = 0.0;
};

struct TestData {
  std::string name;
  int age = 0;
  std::string email;
  bool active = false;
  int order_id = 0;
  double total = 0.0;
  ItemData primary_item;
  std::vector<ItemData> items;
};

// glz::meta 定義（injamm ランタイム用）
template <>
struct glz::meta<TestData> {
  static constexpr auto value = glz::object(
    "name", &TestData::name,
    "age", &TestData::age,
    "email", &TestData::email,
    "active", &TestData::active,
    "order_id", &TestData::order_id,
    "total", &TestData::total,
    "primary_item", &TestData::primary_item,
    "items", &TestData::items
  );
};

template <>
struct glz::meta<ItemData> {
  static constexpr auto value = glz::object(
    "name", &ItemData::name,
    "quantity", &ItemData::quantity,
    "price", &ItemData::price
  );
};

// ============================================================
// 生成されたレンダリング関数をインクルード
// ============================================================

// 各テストケースの生成コードを個別にインクルード
// テンプレート関数なので名前衝突を避けるため名前空間で分離

#include "render1.hpp"
#include "render2.hpp"
#include "render3.hpp"
#include "render4.hpp"
#include "render5.hpp"
#include "render14.hpp"
#include "render15.hpp"
#include "render16.hpp"
#include "render17.hpp"
#include "render18.hpp"

// ============================================================
// テストヘルパ
// ============================================================

int test_count = 0;
int pass_count = 0;

void check(std::string_view name, std::string_view tmpl_str, auto const& data, auto gen_func) {
  ++test_count;

  // injamm ランタイムでレンダリング
  injamm::engine<TestData> eng{std::string(tmpl_str)};
  auto expected = eng.render(data);
  if (!expected) {
    std::cerr << "FAIL [" << name << "] injamm render failed: "
              << expected.error().custom_error_message << "\n";
    return;
  }

  // 生成コードでレンダリング
  auto result = gen_func(data);
  if (!result) {
    std::cerr << "FAIL [" << name << "] generated render failed\n";
    return;
  }

  if (*expected == *result) {
    std::cout << "PASS [" << name << "]\n";
    ++pass_count;
  } else {
    std::cerr << "FAIL [" << name << "]\n";
    std::cerr << "  expected (" << expected->size() << " bytes): [" << *expected << "]\n";
    std::cerr << "  got      (" << result->size() << " bytes): [" << *result << "]\n";
    // バイト単位で比較
    auto const& a = *expected;
    auto const& b = *result;
    for (std::size_t i = 0; i < std::max(a.size(), b.size()); ++i) {
      char ca = (i < a.size()) ? a[i] : '\0';
      char cb = (i < b.size()) ? b[i] : '\0';
      if (ca != cb) {
        std::cerr << "  first diff at offset " << i << ": expected 0x" << std::hex << (int)(unsigned char)ca
                  << " got 0x" << (int)(unsigned char)cb << std::dec << "\n";
        break;
      }
    }
  }
}

void check_into(std::string_view name, std::string_view tmpl_str, auto const& data, auto gen_func) {
  ++test_count;

  injamm::engine<TestData> eng{std::string(tmpl_str)};
  auto expected = eng.render(data);
  if (!expected) {
    std::cerr << "FAIL [" << name << "] injamm render failed: "
              << expected.error().custom_error_message << "\n";
    return;
  }

  std::string out;
  out = "garbage_prefix"; // バッファ再利用の確認: 内容を意図的に汚す
  auto result = gen_func(data, out);
  if (!result) {
    std::cerr << "FAIL [" << name << "] generated render_into failed\n";
    return;
  }

  if (*expected == out) {
    std::cout << "PASS [" << name << "]\n";
    ++pass_count;
  } else {
    std::cerr << "FAIL [" << name << "]\n";
    std::cerr << "  expected (" << expected->size() << " bytes): [" << *expected << "]\n";
    std::cerr << "  got      (" << out.size() << " bytes): [" << out << "]\n";
  }
}

void check_sink(std::string_view name, std::string_view tmpl_str, auto const& data, auto gen_func) {
  ++test_count;

  injamm::engine<TestData> eng{std::string(tmpl_str)};
  auto expected = eng.render(data);
  if (!expected) {
    std::cerr << "FAIL [" << name << "] injamm render failed: "
              << expected.error().custom_error_message << "\n";
    return;
  }

  // 生成コードを sink にストリーミング出力し、文字列版と一致することを確認する
  std::string collected;
  int chunks = 0;
  injamm::callback_sink sink([&](std::string_view sv) {
    collected.append(sv.data(), sv.size());
    ++chunks;
  }, 8);
  auto result = gen_func(data, sink);
  if (!result) {
    std::cerr << "FAIL [" << name << "] generated render_sink failed\n";
    return;
  }
  sink.flush();

  if (*expected == collected) {
    std::cout << "PASS [" << name << "] (chunks=" << chunks << ")\n";
    ++pass_count;
  } else {
    std::cerr << "FAIL [" << name << "]\n";
    std::cerr << "  expected (" << expected->size() << " bytes): [" << *expected << "]\n";
    std::cerr << "  got      (" << collected.size() << " bytes): [" << collected << "]\n";
  }
}

// ============================================================
// テスト実行
// ============================================================

int main() {
  TestData d;
  d.name = "Alice";
  d.age = 30;
  d.email = "alice@example.com";
  d.active = true;
  d.order_id = 12345;
  d.total = 1500.0;
  d.items = {
    {"Widget", 2, 25.0},
    {"Gadget", 1, 99.99},
  };
  d.primary_item = {"Gizmo", 5, 12.5};

  std::cout << "=== injamm_codegen 動作テスト ===\n\n";

  // テスト1: 単純変数
  check("simple vars", "Hello {{name}}, age={{age}}", d,
    [](auto const& data) { return generated::render1(data); });

  // テスト2: フィルタ
  check("filters", "Name: {{name|upper}}, Lower: {{name|lower}}", d,
    [](auto const& data) { return generated::render2(data); });

  // テスト3: セクション
  check("section", "Items:\n{{#items}}\n- {{name}} x{{quantity}}\n{{/items}}", d,
    [](auto const& data) { return generated::render3(data); });

  // テスト4: if/else
  check("if/else", "{{#if active}}Active{{else}}Inactive{{/if}}", d,
    [](auto const& data) { return generated::render4(data); });

  // テスト5: 複合
  check("complex",
    "Order #{{order_id}}:\n{{#if total > 1000}}[VIP]{{/if}}\n{{#items}}\n  {{name}}: ${{price}}\n{{/items}}\nTotal: ${{total}}",
    d,
    [](auto const& data) { return generated::render5(data); });

  // テスト6: bool と数値の直接出力（エスケープ不要パス）
  check("bool and numeric out", "{{name}}|{{active}}|{{age}}", d,
    [](auto const& data) { return generated::render14(data); });

  // テスト7: struct フィールドの JSON 出力 + 生出力
  check("struct field", "{{primary_item}}|{{& primary_item}}", d,
    [](auto const& data) { return generated::render15(data); });

  // テスト8: セクションフィルタ（take が要素数を超えるケースのアンダーフロー防止）
  check("section filter reverse+take overflow", "{{#items | reverse | take(100)}}{{name}};{{/items}}", d,
    [](auto const& data) { return generated::render16(data); });

  // テスト9: セクションフィルタ（take_last が要素数を超えるケースのアンダーフロー防止）
  check("section filter take_last overflow", "{{#items | take_last(100)}}{{name}};{{/items}}", d,
    [](auto const& data) { return generated::render17(data); });

  // テスト10: フィルタの文字列引数・整形系（codegen/VM ドリフト回帰）
  check("filter drift (numify/zerofill/truncate/left/format/replace)",
    "{{order_id | numify}}|{{order_id | zerofill(7)}}|{{email | truncate(8)}}|{{name | left(10)}}|{{total | format(\".2f\")}}|{{name | replace(\"i\",\"X\")}}", d,
    [](auto const& data) { return generated::render18(data); });

  // バッファ再利用版のテスト
  check_into("into simple", "Hello {{name}}, age={{age}}", d,
    [](auto const& data, std::string& out) { return generated::render1(data, out); });
  check_into("into section", "Items:\n{{#items}}\n- {{name}} x{{quantity}}\n{{/items}}", d,
    [](auto const& data, std::string& out) { return generated::render3(data, out); });
  check_into("into if/else", "{{#if active}}Active{{else}}Inactive{{/if}}", d,
    [](auto const& data, std::string& out) { return generated::render4(data, out); });
  check_into("into bool and numeric out", "{{name}}|{{active}}|{{age}}", d,
    [](auto const& data, std::string& out) { return generated::render14(data, out); });
  check_into("into struct field", "{{primary_item}}|{{& primary_item}}", d,
    [](auto const& data, std::string& out) { return generated::render15(data, out); });

  // sink ストリーミング版のテスト
  check_sink("sink simple", "Hello {{name}}, age={{age}}", d,
    [](auto const& data, auto& sink) { return generated::render1(data, sink); });
  check_sink("sink section+if", "Order #{{order_id}}:\n{{#if total > 1000}}[VIP]{{/if}}\n{{#items}}\n  {{name}}: ${{price}}\n{{/items}}\nTotal: ${{total}}", d,
    [](auto const& data, auto& sink) { return generated::render5(data, sink); });

  std::cout << "\n=== 結果: " << pass_count << "/" << test_count << " passed ===\n";
  return (pass_count == test_count) ? 0 : 1;
}
