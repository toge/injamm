/**
 * @file test_codegen_live_partials.cpp
 * @brief partial を含む codegen ライブテストケース（別 TU）
 *
 * @details render_partial / render_partial_impl はテンプレート定義のため、
 *          同一 TU に複数のケースヘッダを置くと再定義になる。partial を含む
 *          ケースは名前空間を分けて個別 TU に配置する
 *          （CMakeLists.txt の add_codegen_case_partial）。
 *
 *          codegen_helpers.hpp をグローバル名前空間で先に include しておくことで、
 *          各ケースヘッダ内（namespace case_*）のヘルパ include（ガード付き）が
 *          no-op になってもヘルパが参照できる。
 */

#include "codegen_helpers.hpp"
#include "codegen_live_check.hpp"

#include "case_partial_loop.hpp"
#include "case_partial_root.hpp"
#include "case_partial_toplevel.hpp"

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
