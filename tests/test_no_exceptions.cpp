// 例外なしスモークテスト: -fno-exceptions でビルドして動作を検証する
// (Catch2 に依存しない)
#include "injamm.hpp"
#include <glaze/glaze.hpp>

#include <cstdint>
#include <cstdio>
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
