/**
 * @file test_codegen_live.cpp
 * @brief injamm_codegen のライブテスト: 生成 → コンパイル → 実行 → VM と比較
 *
 * @details 各ケースのヘッダは CMake のビルド時に injamm_codegen で生成される
 *          (CMakeLists.txt の add_codegen_case)。生成ヘッダがコンパイル不能な
 *          場合はこの実行ファイルのビルド自体が失敗するため、
 *          「壊れたヘッダを exit 0 で書き出す」回帰を ctest で検出できる。
 *          同梱 render*.hpp をリンクする既存 test_codegen とは異なり、
 *          常に最新の codegen 出力を検証する。
 *
 *          partial を含むケースは render_partial 定義の分離のため
 *          test_codegen_live_partials.cpp（別 TU）で検証する。
 */

#include <iostream>
#include <string>

#include "codegen_live_check.hpp"

// ビルド時に生成された各ケースのヘッダ（namespace generated / 関数 case_<name>）
#include "case_inv_bool.hpp"
#include "case_inv_int.hpp"
#include "case_inv_opt.hpp"
#include "case_inv_container.hpp"
#include "case_inv_loop_rootkey.hpp"
#include "case_inv_loop_elemkey.hpp"
#include "case_inv_else.hpp"
#include "case_sec_else_container.hpp"
#include "case_sec_else_scalar.hpp"
#include "case_sec_else_int.hpp"
#include "case_sec_else_in_loop.hpp"
#include "case_opt_section.hpp"
#include "case_opt_else.hpp"
#include "case_at_else.hpp"
#include "case_take_else.hpp"
#include "case_cmp_eq_var.hpp"
#include "case_cmp_ne_var.hpp"
#include "case_cmp_gt_literal.hpp"
#include "case_cmp_gte_literal.hpp"
#include "case_cmp_lt_literal.hpp"
#include "case_cmp_lte_literal.hpp"
#include "case_cmp_string_var.hpp"
#include "case_cmp_string_literal.hpp"
#include "case_cmp_empty_string.hpp"
#include "case_cmp_this_string.hpp"
#include "case_cmp_loop_index.hpp"
#include "case_cmp_loop_index1.hpp"
#include "case_cmp_loop_size.hpp"
#include "case_cmp_loop_roots.hpp"
#include "case_cmp_loop_value_rhs.hpp"
#include "case_cmp_loop_bool.hpp"
#include "case_cmp_string_int.hpp"
#include "case_cmp_invalid_string.hpp"
#include "case_cmp_string_float.hpp"
#include "case_cmp_int_string.hpp"
#include "case_cmp_float_vars.hpp"
#include "case_cmp_optional_rhs.hpp"
#include "case_cmp_optional_lhs.hpp"
#include "case_cmp_enum_literal.hpp"
#include "case_cmp_enum_var.hpp"
#include "case_two_stage_nested_take.hpp"
#include "case_two_stage_nested_index.hpp"
#include "case_if_loop_active.hpp"
#include "case_if_loop_not_active.hpp"
#include "case_if_loop_and.hpp"
#include "case_if_loop_or.hpp"
#include "case_if_loop_filtered.hpp"
#include "case_cmp_bool_section_loop.hpp"
#include "case_cmp_optional_section_loop.hpp"
#include "case_cmp_number_section_loop.hpp"
#include "case_cmp_inherited_bool_loop.hpp"
#include "case_cmp_inherited_optional_loop.hpp"
#include "case_partial_compare_loop.hpp"

/** test_codegen_live_partials.cpp（別 TU）で検証する partial ケースの数 */
int run_partial_loop_checks(CaseData const& d, CaseData const& d2);
int run_partial_root_checks(CaseData const& d, CaseData const& d2);
int run_partial_toplevel_checks(CaseData const& d, CaseData const& d2);
int run_partial_scope_ref_checks(CaseData const& d, CaseData const& d2);
int run_namespace_composition_checks(CaseData const& d);

int main() {
  // truthy 側データセット
  CaseData d;
  // falsy 側データセット: active=false, age=0, opt=空, items=空
  CaseData d2;
  d2.active = false;
  d2.age    = 0;
  d2.opt.reset();
  d2.items.clear();
  d2.qty = 4;
  d2.limit = 3;
  d2.ratio = 3.0;
  d2.ceiling = 2.5;
  d2.name = "S";
  d2.alias = "4";
  d2.numeric = "31";
  d2.invalid_numeric = "0";
  d2.opt_limit.reset();
  d2.level = none;
  d2.other_level = ready;

  CaseData d_partial_falsy = d;
  d_partial_falsy.active   = false;
  CaseData d_empty = d;
  d_empty.name.clear();
  CaseData d_low = d;
  d_low.qty = 2;
  CaseData d_high = d;
  d_high.qty = 4;
  CaseData d_loop_false = d;
  d_loop_false.limit = 4;
  d_loop_false.items[0].count = 4;
  d_loop_false.items[1].count = 4;
  CaseData d_loop_size_false = d;
  d_loop_size_false.items.resize(1);
  CaseData d_numeric_zero = d;
  d_numeric_zero.numeric = "0";
  CaseData d_partial_numeric = d;
  d_partial_numeric.invalid_numeric = "30x";
  CaseData d_root_false = d;
  d_root_false.active = false;
  d_root_false.age = 0;

  std::cout << "=== injamm_codegen ライブテスト（生成→コンパイル→実行→VM 比較） ===\n\n";

  // --- 逆セクション（{{^...}}）: スカラー / optional / コンテナ / ループ内 ---
  live_check("inverted bool", "{{^active}}x{{/active}}", d, d2,
    [](auto const& data) { return generated::case_inv_bool(data); });
  live_check("inverted int", "{{^age}}zero{{/age}}", d, d2,
    [](auto const& data) { return generated::case_inv_int(data); });
  live_check("inverted optional", "{{^opt}}none{{/opt}}", d, d2,
    [](auto const& data) { return generated::case_inv_opt(data); });
  live_check("inverted container", "{{^items}}E{{/items}}", d, d2,
    [](auto const& data) { return generated::case_inv_container(data); });
  // ループ内の逆セクション: 要素に無いキーはルートへフォールバック（VM と同じ二段解決）
  live_check("inverted in loop (root key)", "{{#items}}{{^active}}x{{/active}}{{/items}}", d, d2,
    [](auto const& data) { return generated::case_inv_loop_rootkey(data); });
  // ループ内の逆セクション: 要素自身の bool
  live_check("inverted in loop (elem key)", "{{#items}}{{^on}}y{{/on}}{{/items}}", d, d2,
    [](auto const& data) { return generated::case_inv_loop_elemkey(data); });
  live_check("inverted with else", "{{^active}}off{{else}}on{{/active}}", d, d2,
    [](auto const& data) { return generated::case_inv_else(data); });

  // --- セクションの {{else}}: コンテナ / スカラー / 空コンテナ / ループ内 ---
  live_check("section else (container)", "{{#items}}{{name}}{{else}}empty{{/items}}", d, d2,
    [](auto const& data) { return generated::case_sec_else_container(data); });
  live_check("section else (scalar bool)", "{{#active}}Y{{else}}N{{/active}}", d, d2,
    [](auto const& data) { return generated::case_sec_else_scalar(data); });
  live_check("section else (scalar int)", "{{#age}}N{{else}}Z{{/age}}", d, d2,
    [](auto const& data) { return generated::case_sec_else_int(data); });
  live_check("section else (in loop)", "{{#items}}{{#on}}T{{else}}F{{/on}}{{/items}}", d, d2,
    [](auto const& data) { return generated::case_sec_else_in_loop(data); });
  live_check("section else (index loop)", "{{#items}}{{#loop.is_first}}F{{/loop.is_first}}{{else}}E{{/items}}", d, d2,
    [](auto const& data) { return generated::case_at_else(data); });
  live_check("section else (take)", "{{#items | take(1)}}{{name}}{{else}}E{{/items}}", d, d2,
    [](auto const& data) { return generated::case_take_else(data); });

  // --- std::optional のセクション ---
  live_check("optional section", "{{#opt}}[{{this}}]{{/opt}}", d, d2,
    [](auto const& data) { return generated::case_opt_section(data); });
  live_check("optional section with else", "{{#opt}}[{{this}}]{{else}}none{{/opt}}", d, d2,
    [](auto const& data) { return generated::case_opt_else(data); });

  // --- 比較演算: VM の型付き比較、特殊変数、値→ルート二段解決 ---
  live_check("compare eq variable", "{{#if qty == limit}}Y{{else}}N{{/if}}", d, d_high,
    [](auto const& data) { return generated::case_cmp_eq_var(data); });
  live_check("compare ne variable", "{{#if qty != limit}}Y{{else}}N{{/if}}", d, d_high,
    [](auto const& data) { return generated::case_cmp_ne_var(data); });
  live_check("compare gt literal", "{{#if qty > 2}}Y{{else}}N{{/if}}", d, d_low,
    [](auto const& data) { return generated::case_cmp_gt_literal(data); });
  live_check("compare gte literal", "{{#if qty >= 3}}Y{{else}}N{{/if}}", d, d_low,
    [](auto const& data) { return generated::case_cmp_gte_literal(data); });
  live_check("compare lt literal", "{{#if qty < 4}}Y{{else}}N{{/if}}", d, d_high,
    [](auto const& data) { return generated::case_cmp_lt_literal(data); });
  live_check("compare lte literal", "{{#if qty <= 3}}Y{{else}}N{{/if}}", d, d_high,
    [](auto const& data) { return generated::case_cmp_lte_literal(data); });
  live_check("compare string variable", "{{#if name == alias}}Y{{else}}N{{/if}}", d, d2,
    [](auto const& data) { return generated::case_cmp_string_var(data); });
  live_check("compare string literal", "{{#if name == \"R\"}}Y{{else}}N{{/if}}", d, d2,
    [](auto const& data) { return generated::case_cmp_string_literal(data); });
  live_check("compare empty string", "{{#if name == \"\"}}Y{{else}}N{{/if}}", d, d_empty,
    [](auto const& data) { return generated::case_cmp_empty_string(data); });
  live_check("compare this string", "{{#tags}}{{#if this == \"t1\"}}Y{{else}}N{{/if}}{{/tags}}", d, d2,
    [](auto const& data) { return generated::case_cmp_this_string(data); });
  live_check("compare loop index", "{{#items}}{{#if loop.index == 0}}F{{else}}N{{/if}}{{/items}}", d, d2,
    [](auto const& data) { return generated::case_cmp_loop_index(data); });
  live_check("compare loop index1", "{{#items}}{{#if loop.index1 == 1}}F{{else}}N{{/if}}{{/items}}", d, d2,
    [](auto const& data) { return generated::case_cmp_loop_index1(data); });
  live_check("compare loop size", "{{#items}}{{#if loop.size == 2}}S{{else}}N{{/if}}{{/items}}", d, d_loop_size_false,
    [](auto const& data) { return generated::case_cmp_loop_size(data); });
  live_check("compare loop roots", "{{#items}}{{#if qty == limit}}Y{{else}}N{{/if}}{{/items}}", d, d_loop_false,
    [](auto const& data) { return generated::case_cmp_loop_roots(data); });
  live_check("compare loop value rhs", "{{#items}}{{#if qty == count}}Y{{else}}N{{/if}}{{/items}}", d, d_loop_false,
    [](auto const& data) { return generated::case_cmp_loop_value_rhs(data); });
  live_check("compare loop bool", "{{#items}}{{#if on == active}}Y{{else}}N{{/if}}{{/items}}", d, d_root_false,
    [](auto const& data) { return generated::case_cmp_loop_bool(data); });
  live_check("compare string and integer variable", "{{#if numeric == age}}Y{{else}}N{{/if}}", d, d2,
    [](auto const& data) { return generated::case_cmp_string_int(data); });
  live_check("compare invalid numeric string", "{{#if invalid_numeric == age}}Y{{else}}N{{/if}}", d, d2,
    [](auto const& data) { return generated::case_cmp_invalid_string(data); });
  live_check("compare partial numeric string", "{{#if invalid_numeric == age}}Y{{else}}N{{/if}}", d2, d_partial_numeric,
    [](auto const& data) { return generated::case_cmp_invalid_string(data); });
  live_check("compare string and floating variable", "{{#if numeric == ratio}}Y{{else}}N{{/if}}", d, d_numeric_zero,
    [](auto const& data) { return generated::case_cmp_string_float(data); });
  live_check("compare integer and string variable", "{{#if qty == alias}}Y{{else}}N{{/if}}", d, d2,
    [](auto const& data) { return generated::case_cmp_int_string(data); });
  live_check("compare floating variables", "{{#if ratio >= ceiling}}Y{{else}}N{{/if}}", d, d2,
    [](auto const& data) { return generated::case_cmp_float_vars(data); });
  live_check("compare optional rhs", "{{#if qty == opt_limit}}Y{{else}}N{{/if}}", d, d2,
    [](auto const& data) { return generated::case_cmp_optional_rhs(data); });
  live_check("compare optional lhs", "{{#if opt_limit == qty}}Y{{else}}N{{/if}}", d, d2,
    [](auto const& data) { return generated::case_cmp_optional_lhs(data); });
  live_check("compare enum literal", "{{#if level == 1}}Y{{else}}N{{/if}}", d, d2,
    [](auto const& data) { return generated::case_cmp_enum_literal(data); });
  live_check("compare enum variable", "{{#if level == other_level}}Y{{else}}N{{/if}}", d, d2,
    [](auto const& data) { return generated::case_cmp_enum_var(data); });
  live_check("nested two-stage take", "{{#tags}}{{#items | take(1)}}{{name}}{{/items}}{{/tags}}", d, d2,
    [](auto const& data) { return generated::case_two_stage_nested_take(data); });
  live_check("nested two-stage index", "{{#tags}}{{#items}}{{loop.index}}{{name}}{{/items}}{{/tags}}", d, d2,
    [](auto const& data) { return generated::case_two_stage_nested_index(data); });
  live_check("if root active in loop", "{{#items}}{{#if active}}T{{else}}F{{/if}}{{/items}}", d, d_root_false,
    [](auto const& data) { return generated::case_if_loop_active(data); });
  live_check("if root not active in loop", "{{#items}}{{#if !active}}F{{else}}T{{/if}}{{/items}}", d, d_root_false,
    [](auto const& data) { return generated::case_if_loop_not_active(data); });
  live_check("if root and in loop", "{{#items}}{{#if active && age}}A{{else}}B{{/if}}{{/items}}", d, d_root_false,
    [](auto const& data) { return generated::case_if_loop_and(data); });
  live_check("if root or in loop", "{{#items}}{{#if active || age}}A{{else}}B{{/if}}{{/items}}", d, d_root_false,
    [](auto const& data) { return generated::case_if_loop_or(data); });
  live_check("if root filtered in loop", "{{#items}}{{#if age | gt(18)}}A{{else}}B{{/if}}{{/items}}", d, d_root_false,
    [](auto const& data) { return generated::case_if_loop_filtered(data); });
  live_check("bool section loop metadata at top level",
             "{{#active}}{{#if loop.index == 0}}F{{else}}N{{/if}}{{/active}}", d, d2,
    [](auto const& data) { return generated::case_cmp_bool_section_loop(data); });
  live_check("optional section loop metadata at top level",
             "{{#opt}}{{#if loop.index == 0}}F{{else}}N{{/if}}{{/opt}}", d, d2,
    [](auto const& data) { return generated::case_cmp_optional_section_loop(data); });
  live_check("number section loop metadata at top level",
             "{{#age}}{{#if loop.index == 0}}F{{else}}N{{/if}}{{/age}}", d, d2,
    [](auto const& data) { return generated::case_cmp_number_section_loop(data); });
  live_check("bool section inherits loop metadata",
             "{{#items}}{{#active}}{{#if loop.index == 0}}F{{else}}N{{/if}}{{/active}}{{/items}}",
             d, d_root_false,
    [](auto const& data) { return generated::case_cmp_inherited_bool_loop(data); });
  live_check("optional section inherits loop metadata",
             "{{#items}}{{#opt_limit}}{{#if loop.index == 0}}F{{else}}N{{/if}}{{/opt_limit}}{{/items}}",
             d, d_root_false,
    [](auto const& data) { return generated::case_cmp_inherited_optional_loop(data); });

  // --- partial スコープ: ループ内のグローバル partial は value→root の二段解決 ---
  live_test_count += run_partial_loop_checks(d, d2);
  live_test_count += run_partial_root_checks(d, d2);
  live_test_count += run_partial_toplevel_checks(d, d2);
  live_test_count += run_partial_scope_ref_checks(d, d_partial_falsy);
  live_check(
    "nested partial comparison loop context",
    "{{#partialdef inner}}{{#if this == \"t1\"}}T{{else}}N{{/if}}:"
    "{{#if loop.index == 0}}F{{else}}L{{/if}}:"
    "{{#if loop.index1 == 1}}O{{else}}I{{/if}}:"
    "{{#if loop.size == 3}}S{{else}}X{{/if}}{{/partialdef}}"
    "{{#partialdef outer}}{{>inner}}{{/partialdef}}"
    "{{#tags}}[{{>outer}}]{{/tags}}|{{#items}}[{{>outer}}]{{/items}}",
    d, d2,
    [](auto const& data) { return case_partial_compare_loop::case_partial_compare_loop(data); });
  ++live_test_count;
  std::string public_partial_out;
  auto const public_partial_result =
    case_partial_compare_loop::render_partial<"outer">(d, public_partial_out);
  if (public_partial_result && public_partial_out == "N:L:I:X") {
    std::cout << "PASS [public partial comparison loop context]\n";
    ++live_pass_count;
  } else {
    std::cerr << "FAIL [public partial comparison loop context]\n";
  }
  live_test_count += run_namespace_composition_checks(d);

  std::cout << "\n=== 結果: " << live_pass_count << "/" << live_test_count << " passed ===\n";
  return (live_pass_count == live_test_count) ? 0 : 1;
}
