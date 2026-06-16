#include "injamm.hpp"
#include <glaze/glaze.hpp>
#include <catch2/catch_test_macros.hpp>
#include <vector>

// ---- 日本語テスト用データ型 ----

struct JaUser {
  std::string name;
  int age{};
};

template <>
struct glz::meta<JaUser> {
  static constexpr auto value = glz::object("name", &JaUser::name, "age", &JaUser::age);
};

struct JaUsersData {
  std::vector<JaUser> users;
};

template <>
struct glz::meta<JaUsersData> {
  static constexpr auto value = glz::object("users", &JaUsersData::users);
};

struct JaBoolData {
  bool flag{};
};

template <>
struct glz::meta<JaBoolData> {
  static constexpr auto value = glz::object("flag", &JaBoolData::flag);
};

struct JaNested {
  std::string inner;
};

struct JaOuter {
  std::vector<JaNested> items;
};

template <>
struct glz::meta<JaNested> {
  static constexpr auto value = glz::object("inner", &JaNested::inner);
};

template <>
struct glz::meta<JaOuter> {
  static constexpr auto value = glz::object("items", &JaOuter::items);
};

struct JaIfData {
  std::string name;
  int age{};
};

template <>
struct glz::meta<JaIfData> {
  static constexpr auto value = glz::object("name", &JaIfData::name, "age", &JaIfData::age);
};

struct JaCompany {
  std::string name;
  struct Founder {
    std::string name;
    struct Address {
      std::string city;
      std::string country;
    } address;
  } founder;
};

template <>
struct glz::meta<JaCompany> {
  static constexpr auto value = glz::object("name", &JaCompany::name, "founder", &JaCompany::founder);
};

template <>
struct glz::meta<JaCompany::Founder> {
  static constexpr auto value = glz::object("name", &JaCompany::Founder::name, "address", &JaCompany::Founder::address);
};

template <>
struct glz::meta<JaCompany::Founder::Address> {
  static constexpr auto value = glz::object("city", &JaCompany::Founder::Address::city, "country", &JaCompany::Founder::Address::country);
};

struct JaMapData {
  std::string host{"localhost"};
  int port{8080};
};

template <>
struct glz::meta<JaMapData> {
  static constexpr auto value = glz::object("host", &JaMapData::host, "port", &JaMapData::port);
};

struct JaMapWrapper {
  JaMapData config;
};

template <>
struct glz::meta<JaMapWrapper> {
  static constexpr auto value = glz::object("config", &JaMapWrapper::config);
};

// ---- 基本的な日本語テスト ----

TEST_CASE("ja_literal_hiragana", "[ja]") {
  auto bc = injamm::engine<JaBoolData>("こんにちは");
  auto r = bc.render(JaBoolData{true});
  REQUIRE(r.has_value());
  REQUIRE(*r == "こんにちは");
}

TEST_CASE("ja_literal_katakana", "[ja]") {
  auto bc = injamm::engine<JaBoolData>("カタカナ");
  auto r = bc.render(JaBoolData{true});
  REQUIRE(r.has_value());
  REQUIRE(*r == "カタカナ");
}

TEST_CASE("ja_literal_kanji", "[ja]") {
  auto bc = injamm::engine<JaBoolData>("漢字");
  auto r = bc.render(JaBoolData{true});
  REQUIRE(r.has_value());
  REQUIRE(*r == "漢字");
}

TEST_CASE("ja_literal_mixed", "[ja]") {
  auto bc = injamm::engine<JaBoolData>("日本語テスト");
  auto r = bc.render(JaBoolData{true});
  REQUIRE(r.has_value());
  REQUIRE(*r == "日本語テスト");
}

// ---- 日本語変数テスト ----

TEST_CASE("ja_var_string", "[ja]") {
  JaUser data{"太郎", 25};
  auto bc = injamm::engine<JaUser>("{{name}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "太郎");
}

TEST_CASE("ja_var_multiple", "[ja]") {
  JaUser data{"花子", 30};
  auto bc = injamm::engine<JaUser>("{{name}}:{{age}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "花子:30");
}

TEST_CASE("ja_var_mixed_literal", "[ja]") {
  JaUser data{"一郎", 28};
  auto bc = injamm::engine<JaUser>("名前: {{name}}、年齢: {{age}}歳");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "名前: 一郎、年齢: 28歳");
}

// ---- 日本語セクションテスト ----

TEST_CASE("ja_section", "[ja]") {
  JaUsersData data;
  data.users.push_back(JaUser{"太郎", 25});
  data.users.push_back(JaUser{"次郎", 30});
  auto bc = injamm::engine<JaUsersData>("{{#users}}{{name}}-{{age}}/{{/users}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "太郎-25/次郎-30/");
}

TEST_CASE("ja_section_empty", "[ja]") {
  JaUsersData data;
  auto bc = injamm::engine<JaUsersData>("{{#users}}{{name}}{{/users}}なし");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "なし");
}

TEST_CASE("ja_section_complex", "[ja]") {
  JaUsersData data;
  data.users.push_back(JaUser{"田中", 35});
  data.users.push_back(JaUser{"佐藤", 28});
  data.users.push_back(JaUser{"鈴木", 42});
  auto bc = injamm::engine<JaUsersData>("{{#users}}{{name}}（{{age}}歳）{{#if @last}}。{{else}}、{{/if}}{{/users}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "田中（35歳）、佐藤（28歳）、鈴木（42歳）。");
}

// ---- 日本語逆セクションテスト ----

TEST_CASE("ja_inverted_true", "[ja]") {
  JaBoolData data{true};
  auto bc = injamm::engine<JaBoolData>("{{^flag}}表示しない{{/flag}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "");
}

TEST_CASE("ja_inverted_false", "[ja]") {
  JaBoolData data{false};
  auto bc = injamm::engine<JaBoolData>("{{^flag}}表示する{{/flag}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "表示する");
}

// ---- 日本語 if/else テスト ----

TEST_CASE("ja_if_true", "[ja]") {
  JaIfData data{"太郎", 20};
  auto bc = injamm::engine<JaIfData>("{{#if age}}成人{{/if}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "成人");
}

TEST_CASE("ja_if_false", "[ja]") {
  JaIfData data{"太郎", 0};
  auto bc = injamm::engine<JaIfData>("{{#if age}}成人{{/if}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "");
}

TEST_CASE("ja_if_else", "[ja]") {
  auto bc = injamm::engine<JaIfData>("{{#if age}}成人{{else}}未成年{{/if}}");
  JaIfData data1{"太郎", 20};
  auto r1 = bc.render(data1);
  REQUIRE(r1.has_value());
  REQUIRE(*r1 == "成人");

  JaIfData data2{"次郎", 0};
  auto r2 = bc.render(data2);
  REQUIRE(r2.has_value());
  REQUIRE(*r2 == "未成年");
}

// ---- 日本語ネストテスト ----

TEST_CASE("ja_nested_section", "[ja]") {
  JaOuter data;
  data.items.push_back(JaNested{"東京"});
  data.items.push_back(JaNested{"大阪"});
  auto bc = injamm::engine<JaOuter>("{{#items}}{{inner}}{{/items}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "東京大阪");
}

TEST_CASE("ja_nested_path", "[ja]") {
  JaCompany data{
    .name = "テスト株式会社",
    .founder = JaCompany::Founder{
      .name = "山田太郎",
      .address = {"渋谷区", "東京都"}
    }
  };
  auto bc = injamm::engine<JaCompany>("{{founder.name}}（{{founder.address.city}}）");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "山田太郎（渋谷区）");
}

// ---- 日本語HTMLエスケープテスト ----

TEST_CASE("ja_escaped_output", "[ja]") {
  JaUser data{"<script>alert('XSS')</script>", 25};
  auto bc = injamm::engine<JaUser>("{{name}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "&lt;script&gt;alert(&#x27;XSS&#x27;)&lt;/script&gt;");
}

TEST_CASE("ja_raw_output", "[ja]") {
  JaUser data{"<b>太郎</b>", 25};
  auto bc = injamm::engine<JaUser>("{{{name}}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "<b>太郎</b>");
}

// ---- 日本語特殊文字テスト ----

TEST_CASE("ja_punctuation", "[ja]") {
  auto bc = injamm::engine<JaBoolData>("「日本語」『テスト』【確認】");
  auto r = bc.render(JaBoolData{true});
  REQUIRE(r.has_value());
  REQUIRE(*r == "「日本語」『テスト』【確認】");
}

TEST_CASE("ja_fullwidth_numbers", "[ja]") {
  JaUser data{"テスト", 123};
  auto bc = injamm::engine<JaUser>("{{name}}：{{age}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "テスト：123");
}

TEST_CASE("ja_english_japanese_mixed", "[ja]") {
  JaUser data{"田中太郎", 30};
  auto bc = injamm::engine<JaUser>("Name: {{name}}, Age: {{age}}歳");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "Name: 田中太郎, Age: 30歳");
}

// ---- 日本語フィルタテスト ----

TEST_CASE("ja_filter_upper", "[ja]") {
  JaUser data{"テスト", 25};
  auto bc = injamm::engine<JaUser>("{{name | upper}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "テスト");
}

TEST_CASE("ja_filter_lower", "[ja]") {
  JaUser data{"テスト", 25};
  auto bc = injamm::engine<JaUser>("{{name | lower}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "テスト");
}

TEST_CASE("ja_filter_trim", "[ja]") {
  JaUser data{"  テスト  ", 25};
  auto bc = injamm::engine<JaUser>("{{name | trim}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "テスト");
}

TEST_CASE("ja_filter_truncate", "[ja]") {
  JaUser data{"日本語テスト文字列", 25};
  auto bc = injamm::engine<JaUser>("{{name | truncate(5)}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  CHECK(r->size() > 0);
}

// ---- 日本語繰り返しテスト ----

TEST_CASE("ja_repeated_japanese", "[ja]") {
  auto bc = injamm::engine<JaBoolData>("日本語");
  auto r = bc.render(JaBoolData{true});
  REQUIRE(r.has_value());
  REQUIRE(*r == "日本語");
}

TEST_CASE("ja_long_japanese_string", "[ja]") {
  auto bc = injamm::engine<JaBoolData>("これは非常に長い日本語のテスト文字列です。複数の文字種類を含んでいます。");
  auto r = bc.render(JaBoolData{true});
  REQUIRE(r.has_value());
  REQUIRE(*r == "これは非常に長い日本語のテスト文字列です。複数の文字種類を含んでいます。");
}

// ---- @変数と日本語 ----

TEST_CASE("ja_at_index", "[ja]") {
  JaUsersData data;
  data.users.push_back(JaUser{"一郎", 1});
  data.users.push_back(JaUser{"二郎", 2});
  data.users.push_back(JaUser{"三郎", 3});
  auto bc = injamm::engine<JaUsersData>("{{#users}}{{@index}}:{{name}} {{/users}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "0:一郎 1:二郎 2:三郎 ");
}

TEST_CASE("ja_at_first_last", "[ja]") {
  JaUsersData data;
  data.users.push_back(JaUser{"最初", 1});
  data.users.push_back(JaUser{"中間", 2});
  data.users.push_back(JaUser{"最後", 3});
  auto bc = injamm::engine<JaUsersData>("{{#users}}{{#if @first}}【{{name}}】{{/if}}{{#if @last}}（{{name}}）{{/if}}{{/users}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "【最初】（最後）");
}

// ---- 日本語ネストパステスト ----

TEST_CASE("ja_nested_path_deep", "[ja]") {
  JaCompany data{
    .name = "テスト会社",
    .founder = JaCompany::Founder{
      .name = "山田太郎",
      .address = {"渋谷区", "東京都"}
    }
  };
  auto bc = injamm::engine<JaCompany>("{{founder.address.city}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "渋谷区");
}

// ---- 日本語コンテキストテスト ----

TEST_CASE("ja_template_literal", "[ja]") {
  auto bc = injamm::engine<JaBoolData>("こんにちは世界");
  auto r = bc.render(JaBoolData{true});
  REQUIRE(r.has_value());
  REQUIRE(*r == "こんにちは世界");
}

TEST_CASE("ja_empty_template", "[ja]") {
  auto bc = injamm::engine<JaBoolData>("");
  auto r = bc.render(JaBoolData{true});
  REQUIRE(r.has_value());
  REQUIRE(*r == "");
}

TEST_CASE("ja_only_variables", "[ja]") {
  JaUser data{"太郎", 25};
  auto bc = injamm::engine<JaUser>("{{name}}{{age}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "太郎25");
}

// ---- マップイテレーションと日本語 ----

TEST_CASE("ja_struct_iteration", "[ja]") {
  JaMapWrapper data;
  auto bc = injamm::engine<JaMapWrapper>("{{#config}}{{@key}}={{this}};{{/config}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "host=localhost;port=8080;");
}

// ---- 日本語エラーテスト ----

TEST_CASE("ja_unknown_key", "[ja]") {
  auto bc = injamm::engine<JaUser>("{{ nonexistent }}");
  auto r = bc.render(JaUser{"太郎", 25});
  REQUIRE(!r.has_value());
  REQUIRE(r.error().ec == injamm::error_code::unknown_key);
}

// ---- 日本語break/continueテスト ----

TEST_CASE("ja_break", "[ja]") {
  JaUsersData data{.users = {{"一郎", 1}, {"二郎", 2}, {"三郎", 3}}};
  auto bc = injamm::engine<JaUsersData>("{{#users}}{{name}}{{#break}}...{{/users}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "一郎");
}

TEST_CASE("ja_continue", "[ja]") {
  JaUsersData data{.users = {{"一郎", 1}, {"二郎", 2}, {"三郎", 3}}};
  auto bc = injamm::engine<JaUsersData>("{{#users}}{{#continue}}{{name}}{{/users}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "");
}

// ---- NTTP (コンパイル時) 日本語テスト ----

TEST_CASE("ct_ja_literal", "[ja][ct]") {
  auto constexpr tmpl = injamm::fixed_string("こんにちは");
  auto r = injamm::render<tmpl>(JaBoolData{true});
  REQUIRE(r.has_value());
  REQUIRE(*r == "こんにちは");
}

TEST_CASE("ct_ja_var", "[ja][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{name}}");
  auto r = injamm::render<tmpl>(JaUser{"太郎", 25});
  REQUIRE(r.has_value());
  REQUIRE(*r == "太郎");
}

TEST_CASE("ct_ja_multiple_vars", "[ja][ct]") {
  auto constexpr tmpl = injamm::fixed_string("名前: {{name}}、年齢: {{age}}歳");
  auto r = injamm::render<tmpl>(JaUser{"花子", 30});
  REQUIRE(r.has_value());
  REQUIRE(*r == "名前: 花子、年齢: 30歳");
}

TEST_CASE("ct_ja_section", "[ja][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{#users}}{{name}}-{{age}}/{{/users}}");
  JaUsersData data;
  data.users.push_back(JaUser{"太郎", 25});
  data.users.push_back(JaUser{"次郎", 30});
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "太郎-25/次郎-30/");
}

TEST_CASE("ct_ja_if_else", "[ja][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{#if age}}成人{{else}}未成年{{/if}}");
  auto r1 = injamm::render<tmpl>(JaIfData{"太郎", 20});
  REQUIRE(r1.has_value());
  REQUIRE(*r1 == "成人");

  auto r2 = injamm::render<tmpl>(JaIfData{"次郎", 0});
  REQUIRE(r2.has_value());
  REQUIRE(*r2 == "未成年");
}

TEST_CASE("ct_ja_nested_path", "[ja][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{founder.name}}（{{founder.address.city}}）");
  JaCompany data{
    .name = "テスト会社",
    .founder = JaCompany::Founder{
      .name = "山田太郎",
      .address = {"渋谷区", "東京都"}
    }
  };
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "山田太郎（渋谷区）");
}

TEST_CASE("ct_ja_escaped", "[ja][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{name}}");
  auto r = injamm::render<tmpl>(JaUser{"<b>太郎</b>", 25});
  REQUIRE(r.has_value());
  REQUIRE(*r == "&lt;b&gt;太郎&lt;/b&gt;");
}

TEST_CASE("ct_ja_raw", "[ja][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{{name}}}");
  auto r = injamm::render<tmpl>(JaUser{"<b>太郎</b>", 25});
  REQUIRE(r.has_value());
  REQUIRE(*r == "<b>太郎</b>");
}

TEST_CASE("ct_ja_at_index", "[ja][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{#users}}{{@index}}:{{name}} {{/users}}");
  JaUsersData data;
  data.users.push_back(JaUser{"一郎", 1});
  data.users.push_back(JaUser{"二郎", 2});
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "0:一郎 1:二郎 ");
}

TEST_CASE("ct_ja_at_first_last", "[ja][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{#users}}{{#if @first}}【{{name}}】{{/if}}{{#if @last}}（{{name}}）{{/if}}{{/users}}");
  JaUsersData data;
  data.users.push_back(JaUser{"最初", 1});
  data.users.push_back(JaUser{"最後", 2});
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "【最初】（最後）");
}

TEST_CASE("ct_ja_unknown_key", "[ja][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{ nonexistent }}");
  auto r = injamm::render<tmpl>(JaUser{"太郎", 25});
  REQUIRE(!r.has_value());
  REQUIRE(r.error().ec == injamm::error_code::unknown_key);
}
