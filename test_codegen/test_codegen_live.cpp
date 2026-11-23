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
  CaseData d_partial_falsy = d;
  d_partial_falsy.active   = false;

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

  // --- partial スコープ: ループ内のグローバル partial は value→root の二段解決 ---
  live_test_count += run_partial_loop_checks(d, d2);
  live_test_count += run_partial_root_checks(d, d2);
  live_test_count += run_partial_toplevel_checks(d, d2);
  live_test_count += run_partial_scope_ref_checks(d, d_partial_falsy);
  live_test_count += run_namespace_composition_checks(d);

  std::cout << "\n=== 結果: " << live_pass_count << "/" << live_test_count << " passed ===\n";
  return (live_pass_count == live_test_count) ? 0 : 1;
}
