#include "injamm.hpp"

#include <catch2/catch_test_macros.hpp>

// TLA+ モデル検査 (tlaplus/InjammParser.tla, MC_InjammParser.tla) が検出した
// 反例の回帰テスト。トークンレベルで CT パーサ (ct_parse.hpp) とランタイム VM
// コンパイラ (bytecode_compile.hpp) のセクション処理をモデル化し、全トークン列
// (長さ <= 5) を TLC で列挙した結果、不変条件 InvAgreement / InvCTIllFormed が
// 違反した。2026-11-24 に CT パーサを VM と同じスタック走査に揃えて修正済み
// （本テストは当時落ちていたものが今は通る。回帰保護として残す）。
//
// 反例 1: tin = <<"OpenA", "CloseB">>  →  "{{#a}}{{/b}}"
//   参照仕様 (SYNTAX.md「閉じタグ名の取り違えは構文エラー」) および VM は
//   syntax_error ("mismatched closing tag") を返すが、修正前の CT パーサは
//   閉じタグを発見できないと本体 = 残り全部として黙って成功していた
//   (ct_parse.hpp の閉じタグ探索は同一キーのみを探し、単独閉じタグは
//   366-372 行でスキップ)。
//
// 反例 2: tin = <<"OpenA">>  →  "{{#a}}"
//   VM は unexpected_end を返すが、修正前の CT パーサは無エラーで成功した。
//
// 反例 3: tin = <<"CloseA">>  →  "{{/a}}"
//   VM は stray closing tag (syntax_error) を返すが、修正前の CT パーサは
//   黙ってスキップし後続の解析を続けていた。
//
// 修正後の挙動: 両パスともエラーを返す(仕様どおり一致する)。

namespace {

struct TlaData {
  std::string a;
  std::string b;
};

TlaData make_data() {
  return TlaData{"x", "y"};
}

} // namespace

TEST_CASE("TLA+ counterexample: mismatched close tag {{#a}}{{/b}}", "[tla_counterexample]") {
  auto d = make_data();

  // ランタイム VM は仕様どおり syntax_error を返す(これは通る)
  injamm::engine<TlaData> e("{{#a}}{{/b}}");
  auto vm = e.render(d);
  REQUIRE_FALSE(vm.has_value());
  CHECK(vm.error().ec == injamm::error_code::syntax_error);

  // CT (NTTP render) も同じくエラーになるべき — 現行は成功を返す → ここで落ちる
  auto ct = injamm::render<"{{#a}}{{/b}}">(d);
  INFO("CT path unexpectedly ok=" << ct.has_value());
  REQUIRE_FALSE(ct.has_value());
}

TEST_CASE("TLA+ counterexample: unterminated section {{#a}}", "[tla_counterexample]") {
  auto d = make_data();

  // ランタイム VM は unexpected_end を返す(これは通る)
  injamm::engine<TlaData> e("{{#a}}");
  auto vm = e.render(d);
  REQUIRE_FALSE(vm.has_value());
  CHECK(vm.error().ec == injamm::error_code::unexpected_end);

  // CT (NTTP render) も unexpected_end になるべき — 現行は成功を返す → ここで落ちる
  auto ct = injamm::render<"{{#a}}">(d);
  INFO("CT path unexpectedly ok=" << ct.has_value());
  REQUIRE_FALSE(ct.has_value());
}

TEST_CASE("TLA+ counterexample: stray closing tag {{/a}}", "[tla_counterexample]") {
  auto d = make_data();

  // ランタイム VM は stray closing tag (syntax_error) を返す(これは通る)
  injamm::engine<TlaData> e("x{{/a}}y");
  auto vm = e.render(d);
  REQUIRE_FALSE(vm.has_value());
  CHECK(vm.error().ec == injamm::error_code::syntax_error);

  // CT (NTTP render) も同じくエラーになるべき（修正前は黙ってスキップして成功）
  auto ct = injamm::render<"x{{/a}}y">(d);
  INFO("CT path unexpectedly ok=" << ct.has_value());
  REQUIRE_FALSE(ct.has_value());
}

TEST_CASE("TLA+ counterexample: unterminated if section {{#if a}}", "[tla_counterexample]") {
  auto d = make_data();

  // ランタイム VM は unexpected_end を返す(これは通る)
  injamm::engine<TlaData> e("{{#if a}}");
  auto vm = e.render(d);
  REQUIRE_FALSE(vm.has_value());
  CHECK(vm.error().ec == injamm::error_code::unexpected_end);

  // CT (NTTP render) も同じくエラーになるべき
  auto ct = injamm::render<"{{#if a}}">(d);
  INFO("CT path unexpectedly ok=" << ct.has_value());
  REQUIRE_FALSE(ct.has_value());
}

TEST_CASE("TLA+ counterexample: mismatched close tag inside if", "[tla_counterexample]") {
  auto d = make_data();

  // {{#if a}} の本体内の {{/b}} は VM では mismatched closing tag
  injamm::engine<TlaData> e("{{#if a}}{{/b}}");
  auto vm = e.render(d);
  REQUIRE_FALSE(vm.has_value());
  CHECK(vm.error().ec == injamm::error_code::syntax_error);

  // CT (NTTP render) も同じくエラーになるべき
  auto ct = injamm::render<"{{#if a}}{{/b}}">(d);
  INFO("CT path unexpectedly ok=" << ct.has_value());
  REQUIRE_FALSE(ct.has_value());
}

TEST_CASE("TLA+ counterexample: nested mismatched close tag", "[tla_counterexample]") {
  auto d = make_data();

  // {{#a}} の本体の {{#b}} の内側の {{/a}} は取り違え。SYNTAX.md どおり
  // 両パスとも syntax_error になるべき（修正前の VM は内側の mismatch エラーを
  // 無条件の unexpected_end で上書きしていた）
  injamm::engine<TlaData> e("{{#a}}{{#b}}{{/a}}");
  auto vm = e.render(d);
  REQUIRE_FALSE(vm.has_value());
  CHECK(vm.error().ec == injamm::error_code::syntax_error);

  auto ct = injamm::render<"{{#a}}{{#b}}{{/a}}">(d);
  INFO("CT path unexpectedly ok=" << ct.has_value());
  REQUIRE_FALSE(ct.has_value());
  CHECK(ct.error().ec == injamm::error_code::syntax_error);
}

TEST_CASE("TLA+ counterexample: top-level else tag", "[tla_counterexample]") {
  auto d = make_data();

  // トップレベルの {{else}} は VM では stray closing tag (syntax_error)。
  // 修正前の CT は unknown_key で種別が不一致だった
  injamm::engine<TlaData> e("x{{else}}y");
  auto vm = e.render(d);
  REQUIRE_FALSE(vm.has_value());
  CHECK(vm.error().ec == injamm::error_code::syntax_error);

  auto ct = injamm::render<"x{{else}}y">(d);
  INFO("CT path unexpectedly ok=" << ct.has_value());
  REQUIRE_FALSE(ct.has_value());
  CHECK(ct.error().ec == injamm::error_code::syntax_error);
}

TEST_CASE("TLA+ counterexample: else inside loop section", "[tla_counterexample]") {
  auto d = make_data();

  // loop.* セクションは {{else}} をサポートしない。VM では else で本体が終端し、
  // 後続の閉じタグが stray closing tag (syntax_error) になる。
  // 修正前の CT は {{else}} を未知変数として黙って成功していた
  injamm::engine<TlaData> e("{{#loop.index}}A{{else}}B{{/loop.index}}");
  auto vm = e.render(d);
  REQUIRE_FALSE(vm.has_value());
  CHECK(vm.error().ec == injamm::error_code::syntax_error);

  auto ct = injamm::render<"{{#loop.index}}A{{else}}B{{/loop.index}}">(d);
  INFO("CT path unexpectedly ok=" << ct.has_value());
  REQUIRE_FALSE(ct.has_value());
  CHECK(ct.error().ec == injamm::error_code::syntax_error);
}
