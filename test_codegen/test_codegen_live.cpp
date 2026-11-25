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
#include "case_cmp_rhs_loop_index.hpp"
#include "case_cmp_rhs_this_numeric.hpp"
#include "case_cmp_rhs_loop_index1.hpp"
#include "case_cmp_rhs_loop_size.hpp"
#include "case_cmp_rhs_this_string.hpp"
#include "case_cmp_rhs_loop_parent_index.hpp"
#include "case_cmp_rhs_loop_parent_index1.hpp"
#include "case_cmp_rhs_loop_parent_size.hpp"
#include "case_cmp_lhs_loop_parent_index.hpp"
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
#include "case_size_loop_root_fallback.hpp"
#include "case_size_loop_current_item.hpp"
#include "case_size_root_items_from_tags.hpp"
#include "case_size_loop_vs_root.hpp"
#include "case_size_scalar_age.hpp"
#include "case_size_enum_level.hpp"
#include "case_size_optional_opt.hpp"
#include "case_size_reflectable_profile.hpp"
#include "case_size_scalar_name.hpp"
#include "case_size_container_items.hpp"
#include "case_size_cstring_text_ptr.hpp"
#include "case_partial_compare_loop.hpp"
#include "case_control_break.hpp"
#include "case_control_continue.hpp"
#include "case_control_section_else_break.hpp"
#include "case_control_section_else_continue.hpp"
#include "case_control_inverted_else_break.hpp"
#include "case_control_inverted_else_continue.hpp"
#include "case_control_nested_container_break.hpp"
#include "case_control_nested_container_continue.hpp"
#include "case_control_scalar_break.hpp"
#include "case_control_scalar_continue.hpp"
#include "case_control_optional_break.hpp"
#include "case_control_optional_continue.hpp"
#include "case_control_inverted_break.hpp"
#include "case_control_inverted_continue.hpp"
#include "case_control_top_break.hpp"
#include "case_control_top_continue.hpp"
#include "case_control_join_break.hpp"
#include "case_control_join_continue.hpp"
#include "case_control_reverse_break.hpp"
#include "case_control_stride_continue.hpp"
#include "case_control_take_break.hpp"
#include "case_control_number_section.hpp"
#include "case_control_enum_section.hpp"
#include "case_control_syntax_break.hpp"
#include "case_control_scalar_break_trailing.hpp"
#include "case_control_scalar_continue_trailing.hpp"
#include "case_control_optional_break_trailing.hpp"
#include "case_control_optional_continue_trailing.hpp"
#include "case_control_inverted_body_break_trailing.hpp"
#include "case_control_inverted_body_continue_trailing.hpp"
#include "case_control_inverted_else_break_trailing.hpp"
#include "case_control_inverted_else_continue_trailing.hpp"
#include "case_control_partial_break_trailing.hpp"
#include "case_control_partial_continue_trailing.hpp"
#include "case_string_section.hpp"
#include "case_string_this_section.hpp"
#include "case_string_view_scope.hpp"
#include "case_string_filtered_section.hpp"
#include "case_string_reverse_section.hpp"
#include "case_string_stride_section.hpp"
#include "case_string_index_section.hpp"
#include "case_string_at_section.hpp"
#include "case_string_view_section.hpp"
#include "case_string_ptr_section.hpp"
#include "case_string_control_guard.hpp"
#include "case_string_continue_guard.hpp"
#include "case_string_take_control_guard.hpp"
#include "case_string_reverse_control_guard.hpp"
#include "case_string_stride_control_guard.hpp"

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
  d_empty.items[0].name.clear();
  d_empty.items[1].name.clear();
  CaseStringViewData view_data;
  CaseStringViewData empty_view_data{.short_text = {}};
  CaseStringPointerData ptr_data;
  CaseStringPointerData empty_ptr_data{.text_ptr = ""};
  CaseStringPointerData null_ptr_data{.text_ptr = nullptr};
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
  CaseData d_items_inactive = d;
  d_items_inactive.active = false;

  std::cout << "=== injamm_codegen ライブテスト（生成→コンパイル→実行→VM 比較） ===\n\n";

  live_check("control break", "{{#items}}{{name}}{{#if loop.is_first}}{{else}}{{#break}}{{/if}}{{/items}}Z", d, d2,
    [](auto const& data) { return generated::case_control_break(data); });
  live_check("control continue", "{{#items}}{{#if loop.is_first}}{{else}}{{#continue}}{{/if}}{{name}}{{/items}}Z", d, d2,
    [](auto const& data) { return generated::case_control_continue(data); });
  live_check("control section else break", "{{#items}}{{#active}}{{name}}{{else}}{{#break}}{{/active}}{{/items}}Z", d, d_items_inactive,
    [](auto const& data) { return generated::case_control_section_else_break(data); });
  live_check("control section else continue", "{{#items}}{{#active}}{{name}}{{else}}{{#continue}}{{/active}}{{/items}}Z", d, d_items_inactive,
    [](auto const& data) { return generated::case_control_section_else_continue(data); });
  live_check("control inverted else break", "{{#items}}{{^active}}{{name}}{{else}}{{#break}}{{/active}}{{/items}}Z", d, d_items_inactive,
    [](auto const& data) { return generated::case_control_inverted_else_break(data); });
  live_check("control inverted else continue", "{{#items}}{{^active}}{{name}}{{else}}{{#continue}}{{/active}}{{/items}}Z", d, d_items_inactive,
    [](auto const& data) { return generated::case_control_inverted_else_continue(data); });
  live_check("control nested container break", "{{#items}}[{{#tags}}{{this}}{{#if loop.is_last}}{{else}}{{#break}}{{/if}}{{/tags}}]{{/items}}Z", d, d2,
    [](auto const& data) { return generated::case_control_nested_container_break(data); });
  live_check("control nested container continue", "{{#items}}[{{#tags}}{{#if loop.index}}{{#continue}}{{/if}}{{this}}{{/tags}}]{{/items}}Z", d, d2,
    [](auto const& data) { return generated::case_control_nested_container_continue(data); });
  live_check("control scalar break", "{{#items}}{{#active}}A{{#break}}{{/active}}{{/items}}Z", d, d_items_inactive,
    [](auto const& data) { return generated::case_control_scalar_break(data); });
  live_check("control scalar continue", "{{#items}}{{#active}}A{{#continue}}{{/active}}{{/items}}Z", d, d_items_inactive,
    [](auto const& data) { return generated::case_control_scalar_continue(data); });
  live_check("control optional break", "{{#items}}{{#opt_limit}}A{{#break}}{{/opt_limit}}{{/items}}Z", d, d_items_inactive,
    [](auto const& data) { return generated::case_control_optional_break(data); });
  live_check("control optional continue", "{{#items}}{{#opt_limit}}A{{#continue}}{{/opt_limit}}{{/items}}Z", d, d_items_inactive,
    [](auto const& data) { return generated::case_control_optional_continue(data); });
  live_check("control inverted break", "{{#items}}{{^active}}A{{#break}}{{/active}}{{/items}}Z", d, d_items_inactive,
    [](auto const& data) { return generated::case_control_inverted_break(data); });
  live_check("control inverted continue", "{{#items}}{{^active}}A{{#continue}}{{/active}}{{/items}}Z", d, d_items_inactive,
    [](auto const& data) { return generated::case_control_inverted_continue(data); });
  live_check("control top break", "A{{#break}}B", d, d2,
    [](auto const& data) { return generated::case_control_top_break(data); });
  live_check("control top continue", "A{{#continue}}B", d, d2,
    [](auto const& data) { return generated::case_control_top_continue(data); });
  live_check("control join break", "{{#items | join(\",\")}}{{name}}{{#if loop.is_first}}{{else}}{{#break}}{{/if}}{{/items}}Z", d, d2,
    [](auto const& data) { return generated::case_control_join_break(data); });
  live_check("control join continue", "{{#items | join(\",\")}}{{#if loop.is_first}}{{#continue}}{{/if}}{{name}}{{/items}}Z", d, d2,
    [](auto const& data) { return generated::case_control_join_continue(data); });
  live_check("control reverse break", "{{#items | reverse}}{{name}}{{#if loop.is_first}}{{else}}{{#break}}{{/if}}{{/items}}Z", d, d2,
    [](auto const& data) { return generated::case_control_reverse_break(data); });
  live_check("control stride continue", "{{#tags | stride(1,1)}}{{#if loop.index}}{{#continue}}{{/if}}{{this}}{{/tags}}Z", d, d2,
    [](auto const& data) { return generated::case_control_stride_continue(data); });
  live_check("control take break", "{{#items | take(2)}}{{name}}{{#break}}{{/items}}Z", d, d2,
    [](auto const& data) { return generated::case_control_take_break(data); });
  live_check("control number section", "{{#items}}{{#age}}A{{#continue}}{{/age}}{{/items}}Z", d, d_items_inactive,
    [](auto const& data) { return generated::case_control_number_section(data); });
  live_check("control enum section", "{{#items}}{{#level}}A{{#break}}{{/level}}{{/items}}Z", d, d_items_inactive,
    [](auto const& data) { return generated::case_control_enum_section(data); });
  live_check("SYNTAX break example", "{{#users}}{{name}}{{#if loop.is_last}}.{{else}}{{#break}}{{/if}}{{/users}}", d, d2,
    [](auto const& data) { return generated::case_control_syntax_break(data); });
  live_generated_check("scalar break stops trailing output", "", d,
    [](auto const& data) { return generated::case_control_scalar_break_trailing(data); });
  live_generated_check("scalar continue stops trailing output", "", d,
    [](auto const& data) { return generated::case_control_scalar_continue_trailing(data); });
  live_generated_check("optional break stops trailing output", "", d,
    [](auto const& data) { return generated::case_control_optional_break_trailing(data); });
  live_generated_check("optional continue stops trailing output", "", d,
    [](auto const& data) { return generated::case_control_optional_continue_trailing(data); });
  live_generated_check("inverted body break stops trailing output", "", d,
    [](auto const& data) { return generated::case_control_inverted_body_break_trailing(data); });
  live_generated_check("inverted body continue stops trailing output", "", d,
    [](auto const& data) { return generated::case_control_inverted_body_continue_trailing(data); });
  live_generated_check("inverted else break stops trailing output", "", d,
    [](auto const& data) { return generated::case_control_inverted_else_break_trailing(data); });
  live_generated_check("inverted else continue stops trailing output", "", d,
    [](auto const& data) { return generated::case_control_inverted_else_continue_trailing(data); });
  live_generated_check("partial break stops trailing output", "PZ", d,
    [](auto const& data) { return generated::case_control_partial_break_trailing(data); });
  live_generated_check("partial continue stops trailing output", "PPZ", d,
    [](auto const& data) { return generated::case_control_partial_continue_trailing(data); });

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

  live_check("string section", "{{#items}}{{#name}}[{{name}}]{{/name}}{{/items}}", d, d_empty,
    [](auto const& data) { return generated::case_string_section(data); });
  live_check("string this section", "{{#items}}{{#name}}[{{this}}]{{/name}}{{/items}}", d, d_empty,
    [](auto const& data) { return generated::case_string_this_section(data); });
  live_check("string filtered section", "{{#items}}{{#name | take(1)}}[{{this}}]{{/name}}{{/items}}", d, d_empty,
    [](auto const& data) { return generated::case_string_filtered_section(data); });
  live_check("string reverse section", "{{#items}}{{#name | reverse}}[{{name}}]{{/name}}{{/items}}", d, d_empty,
    [](auto const& data) { return generated::case_string_reverse_section(data); });
  live_check("string stride section", "{{#items}}{{#name | stride(1,1)}}[{{name}}]{{/name}}{{/items}}", d, d_empty,
    [](auto const& data) { return generated::case_string_stride_section(data); });
  live_check("string index section", "{{#items}}{{#name}}{{loop.index}}:{{loop.index1}}:{{loop.is_first}}:{{loop.is_last}}:{{loop.size}}:{{this}}{{/name}}{{/items}}", d, d_empty,
    [](auto const& data) { return generated::case_string_index_section(data); });
  live_check("string at section", "{{#items}}{{#name}}{{#loop.is_first}}F{{/loop.is_first}}{{^loop.is_last}}N{{/loop.is_last}}{{/name}}{{/items}}", d, d_empty,
    [](auto const& data) { return generated::case_string_at_section(data); });
  live_check("string_view section", "{{#short_text}}[{{this}}]{{/short_text}}", view_data, empty_view_data,
    [](auto const& data) { return generated::case_string_view_section(data); });
  live_check("string_view scope", "{{#short_text}}[{{this}}|{{short_text}}]{{/short_text}}", view_data, empty_view_data,
    [](auto const& data) { return generated::case_string_view_scope(data); });
  live_generated_check("string_ptr section", "[{}|PTR]", ptr_data,
    [](auto const& data) { return generated::case_string_ptr_section(data); });
  live_generated_check("string_ptr empty section", "", empty_ptr_data,
    [](auto const& data) { return generated::case_string_ptr_section(data); });
  live_check("string control guard", "{{#items}}{{#name}}{{#break}}{{name}}{{/name}}X{{/items}}", d, d_empty,
    [](auto const& data) { return generated::case_string_control_guard(data); });
  live_check("string continue guard", "{{#items}}{{#name}}{{#continue}}{{name}}{{/name}}Y{{/items}}", d, d_empty,
    [](auto const& data) { return generated::case_string_continue_guard(data); });
  live_check("string take control guard", "{{#items}}{{#name | take(1)}}{{#break}}{{name}}{{/name}}T{{/items}}", d, d_empty,
    [](auto const& data) { return generated::case_string_take_control_guard(data); });
  live_check("string reverse control guard", "{{#items}}{{#name | reverse}}{{#continue}}{{name}}{{/name}}R{{/items}}", d, d_empty,
    [](auto const& data) { return generated::case_string_reverse_control_guard(data); });
  live_check("string stride control guard", "{{#items}}{{#name | stride(1,1)}}{{#break}}{{name}}{{/name}}S{{/items}}", d, d_empty,
    [](auto const& data) { return generated::case_string_stride_control_guard(data); });

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
  live_check("compare rhs loop index", "{{#items}}{{#if qty == loop.index}}Y{{else}}N{{/if}}{{/items}}", d, d2,
    [](auto const& data) { return generated::case_cmp_rhs_loop_index(data); });
  live_check("compare rhs this numeric", "{{#items}}{{#if loop.index == this}}Y{{else}}N{{/if}}{{/items}}", d, d2,
    [](auto const& data) { return generated::case_cmp_rhs_this_numeric(data); });
  live_check("compare rhs loop index1", "{{#items}}{{#if qty == loop.index1}}Y{{else}}N{{/if}}{{/items}}", d, d2,
    [](auto const& data) { return generated::case_cmp_rhs_loop_index1(data); });
  live_check("compare rhs loop size", "{{#items}}{{#if qty == loop.size}}Y{{else}}N{{/if}}{{/items}}", d, d2,
    [](auto const& data) { return generated::case_cmp_rhs_loop_size(data); });
  live_check("compare rhs this string", "{{#items}}{{#if name == this}}Y{{else}}N{{/if}}{{/items}}", d, d2,
    [](auto const& data) { return generated::case_cmp_rhs_this_string(data); });
  live_generated_check("compare rhs loop.parent.index",
                       "YNNNYN",
                       d,
    [](auto const& data) { return generated::case_cmp_rhs_loop_parent_index(data); });
  live_generated_check("compare rhs loop.parent.index/falsy",
                       "",
                       d2,
    [](auto const& data) { return generated::case_cmp_rhs_loop_parent_index(data); });
  live_generated_check("compare rhs loop.parent.index1",
                       "YNNNYN",
                       d,
    [](auto const& data) { return generated::case_cmp_rhs_loop_parent_index1(data); });
  live_generated_check("compare rhs loop.parent.index1/falsy",
                       "",
                       d2,
    [](auto const& data) { return generated::case_cmp_rhs_loop_parent_index1(data); });
  live_generated_check("compare rhs loop.parent.size",
                       "YYYYYY",
                       d,
    [](auto const& data) { return generated::case_cmp_rhs_loop_parent_size(data); });
  live_generated_check("compare rhs loop.parent.size/falsy",
                       "",
                       d2,
    [](auto const& data) { return generated::case_cmp_rhs_loop_parent_size(data); });
  live_generated_check("compare lhs loop.parent.index",
                       "YNNNYN",
                       d,
    [](auto const& data) { return generated::case_cmp_lhs_loop_parent_index(data); });
  live_generated_check("compare lhs loop.parent.index/falsy",
                       "",
                       d2,
    [](auto const& data) { return generated::case_cmp_lhs_loop_parent_index(data); });
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
  live_check("size loop root fallback",
             "{{#items}}{{tags.size}}{{/items}}",
             d, d2,
    [](auto const& data) { return generated::case_size_loop_root_fallback(data); });
  live_check("size loop current item",
             "{{#items}}{{name.size}}{{/items}}",
             d, d2,
    [](auto const& data) { return generated::case_size_loop_current_item(data); });
  live_check("size root items from tags",
             "{{#tags}}{{items.size}}{{/tags}}",
             d, d2,
    [](auto const& data) { return generated::case_size_root_items_from_tags(data); });
  live_check("size loop vs root",
             "{{#items}}{{name.size}}|{{root.name.size}}{{/items}}",
             d, d2,
    [](auto const& data) { return generated::case_size_loop_vs_root(data); });
  live_check("size scalar age",
             "{{age.size}}",
             d, d2,
    [](auto const& data) { return generated::case_size_scalar_age(data); });
  live_check("size enum level",
             "{{level.size}}",
             d, d2,
    [](auto const& data) { return generated::case_size_enum_level(data); });
  live_check("size optional opt",
             "{{opt.size}}",
             d, d2,
    [](auto const& data) { return generated::case_size_optional_opt(data); });
  live_check("size reflectable profile",
             "{{profile.size}}",
             d, d2,
    [](auto const& data) { return generated::case_size_reflectable_profile(data); });
  live_check("size scalar name",
             "{{name.size}}",
             d, d2,
    [](auto const& data) { return generated::case_size_scalar_name(data); });
  live_check("size container items",
             "{{items.size}}",
             d, d2,
    [](auto const& data) { return generated::case_size_container_items(data); });
  live_generated_check("size cstring pointer",
                       "3",
                       ptr_data,
    [](auto const& data) { return generated::case_size_cstring_text_ptr(data); });
  live_generated_check("size null cstring pointer",
                       "0",
                       null_ptr_data,
    [](auto const& data) { return generated::case_size_cstring_text_ptr(data); });

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
