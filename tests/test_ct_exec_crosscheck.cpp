#include "injamm.hpp"

#include <catch2/catch_test_macros.hpp>
#include <glaze/glaze.hpp>

// 単純テンプレート（コンパイル時アンロール経路）と複雑テンプレート（VM フォールバック）を
// engine<T>（ランタイム VM）と cross-check する。NTTP のアンロール実行と VM の出力が
// 常に一致することを保証する（staged interpreter の意味論は VM と同一）。

struct CrossData {
  std::string              name;
  int                      age{};
  double                   score{};
  std::vector<std::string> tags;
};

template <>
struct glz::meta<CrossData> {
  static constexpr auto value = glz::object("name", &CrossData::name, "age", &CrossData::age, "score", &CrossData::score, "tags", &CrossData::tags);
};

CrossData make_data() {
  return CrossData{"Alice", 30, 91.5, {"fast", "lazy"}};
}

std::string engine_render(char const* tmpl, CrossData const& d) {
  injamm::engine<CrossData> e(tmpl);
  auto                      r = e.render(d);
  REQUIRE(r.has_value());
  return *r;
}

#define CROSSCHECK(LIT)                 \
  do {                                  \
    auto r = injamm::render<LIT>(d);    \
    REQUIRE(r.has_value());             \
    CHECK(*r == engine_render(LIT, d)); \
  } while (0)

TEST_CASE("ct_exec cross-check: simple templates (unrolled path)", "[ct_exec][crosscheck]") {
  auto d = make_data();

  // これらは単純テンプレートであり、必ずコンパイル時アンロール経路を通ることを確認する
  static_assert(injamm::detail::ct_is_unrollable(injamm::detail::nttp_render_data<injamm::fixed_string{"{{name}}"}, false, false, CrossData>::ct_bc), "expected unrollable");
  static_assert(injamm::detail::ct_is_unrollable(injamm::detail::nttp_render_data<injamm::fixed_string{"test example, {{name}} = {{age}} on {{score}}"}, false, false, CrossData>::ct_bc),
                "expected unrollable");
  static_assert(injamm::detail::ct_is_unrollable(injamm::detail::nttp_render_data<injamm::fixed_string{"{{name}} {{{name}}} {{& name}}"}, false, false, CrossData>::ct_bc), "expected unrollable");

  CROSSCHECK("{{name}}");
  CROSSCHECK("{{name}} {{age}}");
  CROSSCHECK("test example, {{name}} = {{age}} on {{score}}");
  CROSSCHECK("{{name}}{{age}}{{score}}{{name}}");
  CROSSCHECK("{{name}} = {{age}} and {{name}} on {{score}} points");
  CROSSCHECK("5 vars: {{name}}/{{age}}/{{score}}/{{name}}/{{age}}");
  CROSSCHECK("{{{name}}} {{{age}}} {{{score}}}");
  CROSSCHECK("{{name}} {{{name}}} {{& name}}");
}

TEST_CASE("ct_exec cross-check: complex templates (VM fallback)", "[ct_exec][crosscheck]") {
  auto d = make_data();

  // セクション / ループ / @変数を含むため、必ずランタイム VM にフォールバックする
  static_assert(!injamm::detail::ct_is_unrollable(injamm::detail::nttp_render_data<injamm::fixed_string{"{{#tags}}[{{this}}]{{/tags}}"}, false, false, CrossData>::ct_bc), "expected fallback");
  static_assert(!injamm::detail::ct_is_unrollable(injamm::detail::nttp_render_data<injamm::fixed_string{"{{#tags}}{{@index}}:{{this}};{{/tags}}"}, false, false, CrossData>::ct_bc),
                "expected fallback");

  CROSSCHECK("{{#tags}}[{{this}}]{{/tags}}");
  CROSSCHECK("{{#age}}age is {{age}}{{/age}}");
  CROSSCHECK("{{^age}}no age{{/age}}");
  CROSSCHECK("{{#tags}}[{{.}}]{{/tags}}");
  CROSSCHECK("{{#tags}}{{@index}}:{{this}};{{/tags}}");
  CROSSCHECK("{{#name}}hi {{name}}{{/name}}");
  CROSSCHECK("{{age}} {{#tags}}[{{this}}]{{/tags}} {{score}}");
  CROSSCHECK("{{#tags}}{{this}}{{/tags}} and {{name}}");
}

#undef CROSSCHECK

// ---- 直線 only partial のコンパイル時アンロール（htmx の行更新用途） ----

TEST_CASE("ct_exec cross-check: straight-line partial (compile-time unroll)", "[ct_exec][crosscheck][partial]") {
  auto d = make_data();

  // {{#partialdef row}} の本文が「リテラル + 変数のみ」= 直線 → コンパイル時アンロール可能であること
  constexpr auto kTmpl = injamm::fixed_string(
      "<table>{{#partialdef row}}<tr><td>{{name}}</td><td>{{age}}</td></tr>{{/partialdef}}{{> row}}</table>");
  using D        = injamm::detail::nttp_render_data<kTmpl, false, false, CrossData>;
  constexpr auto pidx = [] {
    for (std::size_t i = 0; i < D::parsed.partial_count; ++i)
      if (D::parsed.partial_names[i] == "row")
        return i;
    return static_cast<std::size_t>(-1);
  }();
  static_assert(pidx != static_cast<std::size_t>(-1), "row partial not found");
  constexpr auto body_start = D::parsed.partial_body_starts[pidx];
  constexpr auto body_end   = D::parsed.partial_body_ends[pidx];
  using BodyD               = injamm::detail::nttp_partial_body_data<kTmpl, body_start, body_end, false, false, CrossData>;
  static_assert(injamm::detail::ct_is_unrollable(BodyD::ct_bc), "straight-line partial should be unrolled");

  // コンパイル時アンロール経路の出力
  auto r = injamm::render_partial<kTmpl, injamm::fixed_string{"row"}>(d);
  REQUIRE(r.has_value());
  CHECK(*r == "<tr><td>Alice</td><td>30</td></tr>");

  // ランタイム VM（engine::render(value, "row")）と一致するか
  injamm::engine<CrossData> e("<table>{{#partialdef row}}<tr><td>{{name}}</td><td>{{age}}</td></tr>{{/partialdef}}{{> row}}</table>");
  auto                      er = e.render(d, "row");
  REQUIRE(er.has_value());
  CHECK(*r == *er);
}

TEST_CASE("ct_exec cross-check: straight-line partial with raw/escaped mix", "[ct_exec][crosscheck][partial]") {
  auto d = make_data();
  constexpr auto kTmpl =
      injamm::fixed_string("{{#partialdef line}}{{name}} {{{name}}} {{age}}{{/partialdef}}{{> line}}");
  auto r = injamm::render_partial<kTmpl, injamm::fixed_string{"line"}>(d);
  REQUIRE(r.has_value());
  CHECK(*r == "Alice Alice 30");

  injamm::engine<CrossData> e("{{#partialdef line}}{{name}} {{{name}}} {{age}}{{/partialdef}}{{> line}}");
  auto                      er = e.render(d, "line");
  REQUIRE(er.has_value());
  CHECK(*r == *er);
}

TEST_CASE("ct_exec cross-check: section partial (VM fallback, still correct)", "[ct_exec][crosscheck][partial]") {
  auto d = make_data();

  // セクションを含む partial 本文は直線でないため VM フォールバックするが、結果は正しい
  constexpr auto kTmpl =
      injamm::fixed_string("{{#partialdef list}}<ul>{{#tags}}<li>{{this}}</li>{{/tags}}</ul>{{/partialdef}}{{> list}}");
  auto r = injamm::render_partial<kTmpl, injamm::fixed_string{"list"}>(d);
  REQUIRE(r.has_value());
  CHECK(*r == "<ul><li>fast</li><li>lazy</li></ul>");

  injamm::engine<CrossData> e("{{#partialdef list}}<ul>{{#tags}}<li>{{this}}</li>{{/tags}}</ul>{{/partialdef}}{{> list}}");
  auto                      er = e.render(d, "list");
  REQUIRE(er.has_value());
  CHECK(*r == *er);
}

TEST_CASE("ct_exec cross-check: mixed template (unrolled prefix/suffix + VM section)", "[ct_exec][crosscheck]") {
  auto d = make_data();

  // 直線区間 + セクションの混在テンプレートがハイブリッド経路で正しく出力されること
  constexpr auto kTmpl = injamm::fixed_string("{{name}} tags: {{#tags}}[{{this}}]{{/tags}} ok");
  auto r = injamm::render<kTmpl>(d);
  REQUIRE(r.has_value());
  CHECK(*r == "Alice tags: [fast][lazy] ok");

  injamm::engine<CrossData> e("{{name}} tags: {{#tags}}[{{this}}]{{/tags}} ok");
  auto                      er = e.render(d);
  REQUIRE(er.has_value());
  CHECK(*r == *er);
}