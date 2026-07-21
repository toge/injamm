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

namespace gen1 {
#include "render1.hpp"
}

namespace gen2 {
#include "render2.hpp"
}

namespace gen3 {
#include "render3.hpp"
}

namespace gen4 {
#include "render4.hpp"
}

namespace gen5 {
#include "render5.hpp"
}

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

  std::cout << "=== injamm_codegen 動作テスト ===\n\n";

  // テスト1: 単純変数
  check("simple vars", "Hello {{name}}, age={{age}}", d,
    [](auto const& data) { return gen1::generated::render(data); });

  // テスト2: フィルタ
  check("filters", "Name: {{name|upper}}, Lower: {{name|lower}}", d,
    [](auto const& data) { return gen2::generated::render(data); });

  // テスト3: セクション
  check("section", "Items:\n{{#items}}\n- {{name}} x{{quantity}}\n{{/items}}", d,
    [](auto const& data) { return gen3::generated::render(data); });

  // テスト4: if/else
  check("if/else", "{{#if active}}Active{{else}}Inactive{{/if}}", d,
    [](auto const& data) { return gen4::generated::render(data); });

  // テスト5: 複合
  check("complex",
    "Order #{{order_id}}:\n{{#if total > 1000}}[VIP]{{/if}}\n{{#items}}\n  {{name}}: ${{price}}\n{{/items}}\nTotal: ${{total}}",
    d,
    [](auto const& data) { return gen5::generated::render(data); });

  std::cout << "\n=== 結果: " << pass_count << "/" << test_count << " passed ===\n";
  return (pass_count == test_count) ? 0 : 1;
}
