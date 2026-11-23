/**
 * @file test_codegen_live_partials.cpp
 * @brief partial を含む codegen ライブテストケース（別 TU）
 *
 * @details 各ケースは個別の名前空間へ生成してスコープ解決を分離し、
 *          同一 TU から VM と生成コードを比較する
 *          （CMakeLists.txt の add_codegen_case_partial）。
 *
 *          生成ヘッダは codegen_helpers.hpp をグローバル名前空間へ include するため、
 *          異なるケース名前空間を同じ TU で使用してもヘルパ定義が欠落しない。
 */

#include "codegen_live_check.hpp"

#include "case_partial_loop.hpp"
#include "case_partial_root.hpp"
#include "case_partial_toplevel.hpp"
#include "case_partial_scope_refs.hpp"

int run_partial_loop_checks(CaseData const& d, CaseData const& d2) {
  live_check("global partial in loop",
             "{{#partialdef g}}{{name}}{{/partialdef}}{{#items}}{{>g}}{{/items}}", d, d2,
    [](auto const& data) { return case_partial_loop::case_partial_loop(data); });
  return 0;
}

int run_partial_root_checks(CaseData const& d, CaseData const& d2) {
  live_check("global partial root ref in loop",
             "{{#partialdef h}}{{root.name}}-{{name}}{{/partialdef}}{{#items}}{{>h}}{{/items}}", d, d2,
    [](auto const& data) { return case_partial_root::case_partial_root(data); });
  return 0;
}

int run_partial_toplevel_checks(CaseData const& d, CaseData const& d2) {
  live_check("global partial at top level",
             "{{#partialdef t}}{{name}}{{/partialdef}}{{>t}}", d, d2,
    [](auto const& data) { return case_partial_toplevel::case_partial_toplevel(data); });
  return 0;
}

int run_partial_scope_ref_checks(CaseData const& d, CaseData const& d2) {
  live_check("global partial reference scope",
             "{{#partialdef s}}{{root_only}}|{{profile.name}}|{{profile_2E_name}}|{{root_only | upper}}|"
             "{{#if active}}T{{else}}F{{/if}}|{{#if age > 18}}G{{else}}S{{/if}}|"
             "{{#if active && age}}A{{else}}B{{/if}}|"
             "{{#items | take(1)}}S:{{name}}{{/items}}|"
             "{{#items}}I:{{loop.index}}{{/items}}{{/partialdef}}"
             "{{#items}}[{{>s}}]{{/items}}",
             d, d2,
    [](auto const& data) { return case_partial_scope_refs::case_partial_scope_refs(data); });
  return 0;
}
