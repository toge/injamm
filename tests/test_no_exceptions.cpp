// 例外なしスモークテスト: -fno-exceptions でビルドして動作を検証する
// (Catch2 に依存しない)
#include "injamm.hpp"
#include <glaze/glaze.hpp>

#include <cstdint>
#include <cstdio>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

} // namespace

struct smoke_user {
  std::string name;
  int age{};
};

template <>
struct glz::meta<smoke_user> {
  static constexpr auto value = glz::object("name", &smoke_user::name, "age", &smoke_user::age);
};

struct smoke_group {
  std::string title;
  std::vector<smoke_user> users;
  bool flag{};
};

template <>
struct glz::meta<smoke_group> {
  static constexpr auto value =
      glz::object("title", &smoke_group::title, "users", &smoke_group::users, "flag", &smoke_group::flag);
};

struct smoke_min {
  long long n{};
};

template <>
struct glz::meta<smoke_min> {
  static constexpr auto value = glz::object("n", &smoke_min::n);
};

int main() {
  const smoke_group data{"team", {{"Alice", 30}, {"Bob", 20}}, false};

  // NTTP コンパイル時レンダリング + glaze フィールド解決 + セクション
  {
    auto r = injamm::render<"{{title}}: {{#users}}{{name}}({{age}}){{/users}}">(data);
    check(r.has_value(), "NTTP render has_value");
    if (r.has_value()) {
      check(*r == "team: Alice(30)Bob(20)", "NTTP render value");
    }
  }

  // engine VM (compile / render)
  {
    injamm::engine<smoke_group> eng{"{{title}}/{{#users}}{{name}},{{/users}}"};
    auto r = eng.render(data);
    check(r.has_value(), "engine render has_value");
    if (r.has_value()) {
      check(*r == "team/Alice,Bob,", "engine render value");
    }
  }

  // 文字列フィルタ
  {
    auto r = injamm::render<"{{#users}}{{name | upper}},{{/users}}">(data);
    check(r.has_value(), "filter render has_value");
    if (r.has_value()) {
      check(*r == "ALICE,BOB,", "filter render value");
    }
  }

  // 逆セクション (flag は false なので描画される)
  {
    auto r = injamm::render<"{{^flag}}none{{/flag}}">(data);
    check(r.has_value(), "inverted section has_value");
    if (r.has_value()) {
      check(*r == "none", "inverted section value");
    }
  }

  // HTML エスケープあり / なし ({{{var}}})
  {
    const smoke_user esc{"<b>&", 0};
    auto e = injamm::engine<smoke_user>{"{{name}}|{{{name}}}"};
    auto r = e.render(esc);
    check(r.has_value(), "escape render has_value");
    if (r.has_value()) {
      check(*r == "&lt;b&gt;&amp;|<b>&", "escape render value");
    }
  }

  // 推定容量を超える長い変数が切り詰められないこと（監査 finding 1）
  {
    const smoke_user long_name{std::string(1000, 'x'), 0};
    auto e = injamm::engine<smoke_user>{"Value: {{name}}"};
    auto r = e.render(long_name);
    check(r.has_value(), "long value has_value");
    if (r.has_value()) check(r->size() == 1007, "long value not truncated");
  }

  // セクションの大量出力が切り詰められないこと
  {
    smoke_group many{"t", {}, false};
    for (int i = 0; i < 1000; ++i) many.users.push_back({"abcdefghij", i});
    auto e = injamm::engine<smoke_group>{"{{#users}}{{name}},{{/users}}"};
    auto r = e.render(many);
    check(r.has_value(), "long section has_value");
    if (r.has_value()) check(r->size() == 11000, "long section not truncated");
  }

  // 不正フォーマットは abort せず invalid_format を返す
  {
    auto r = injamm::render<"{{age | format(\"q\")}}">(smoke_user{"x", 30});
    check(!r.has_value() && r.error().ec == injamm::error_code::invalid_format,
          "invalid format returns invalid_format");
  }

  // bin フィルタが LLONG_MIN を変換できること
  {
    auto r = injamm::render<"{{n | bin}}">(smoke_min{std::numeric_limits<long long>::min()});
    const std::string expected = "-1000000000000000000000000000000000000000000000000000000000000000";
    check(r.has_value() && *r == expected, "bin LLONG_MIN");
  }

  // span ベースのバイトコード保存 / 読み込み
  {
    injamm::engine<smoke_group> eng{"{{#users}}{{name}},{{/users}}"};
    std::vector<std::uint8_t> buf;
    auto ec = injamm::save_bytecode(eng.get_bytecode(), buf);
    check(ec == injamm::error_code::none, "save_bytecode span");
    auto loaded = injamm::load_bytecode<smoke_group>(std::span<const std::uint8_t>(buf));
    check(loaded.has_value(), "load_bytecode span");
    if (loaded.has_value()) {
      injamm::engine<smoke_group> eng2{std::move(*loaded)};
      auto r = eng2.render(data);
      check(r.has_value(), "render loaded bytecode has_value");
      if (r.has_value()) {
        check(*r == "Alice,Bob,", "render loaded bytecode value");
      }
    }
  }

  if (failures == 0) {
    std::printf("no_exceptions smoke: all checks passed\n");
    return 0;
  }
    std::printf("no_exceptions smoke: %d checks failed\n", failures);
  return 1;
}
