#include "injamm.hpp"
#include <glaze/glaze.hpp>
#include <catch2/catch_test_macros.hpp>
#include <climits>
#include <map>
#include <optional>
#include <set>
#include <vector>

// ---- NTTP テスト用データ型 ----

struct CtUser {
  std::string name;
  int age{};
};

template <>
struct glz::meta<CtUser> {
  static constexpr auto value = glz::object("name", &CtUser::name, "age", &CtUser::age);
};

struct CtUsersData {
  std::vector<CtUser> users;
};

template <>
struct glz::meta<CtUsersData> {
  static constexpr auto value = glz::object("users", &CtUsersData::users);
};

struct CtBoolData {
  bool flag{};
};

template <>
struct glz::meta<CtBoolData> {
  static constexpr auto value = glz::object("flag", &CtBoolData::flag);
};

struct CtNested {
  std::string inner;
};

struct CtOuter {
  std::vector<CtNested> items;
};

template <>
struct glz::meta<CtNested> {
  static constexpr auto value = glz::object("inner", &CtNested::inner);
};

template <>
struct glz::meta<CtOuter> {
  static constexpr auto value = glz::object("items", &CtOuter::items);
};

struct CtAddress {
  std::string city;
  std::string country;
};

struct CtFounder {
  std::string name;
  CtAddress address;
};

struct CtCompany {
  std::string name;
  CtFounder founder;
};

template <>
struct glz::meta<CtAddress> {
  static constexpr auto value = glz::object("city", &CtAddress::city, "country", &CtAddress::country);
};

template <>
struct glz::meta<CtFounder> {
  static constexpr auto value = glz::object("name", &CtFounder::name, "address", &CtFounder::address);
};

template <>
struct glz::meta<CtCompany> {
  static constexpr auto value = glz::object("name", &CtCompany::name, "founder", &CtCompany::founder);
};

struct CtIfData {
  std::string name;
  int age{};
};

template <>
struct glz::meta<CtIfData> {
  static constexpr auto value = glz::object("name", &CtIfData::name, "age", &CtIfData::age);
};

struct CtOptionalData {
  std::optional<std::string> opt_str;
};

template <>
struct glz::meta<CtOptionalData> {
  static constexpr auto value = glz::object("opt_str", &CtOptionalData::opt_str);
};

struct CtFloatData {
  double value{};
};

template <>
struct glz::meta<CtFloatData> {
  static constexpr auto value = glz::object("value", &CtFloatData::value);
};

struct CtLlData {
  long long val{};
};

template <>
struct glz::meta<CtLlData> {
  static constexpr auto value = glz::object("val", &CtLlData::val);
};

struct CtRootData {
  std::string app_name{"injamm"};
  struct Inner {
    std::string version{"1.0"};
  } info{.version = "1.0"};
};

template <>
struct glz::meta<CtRootData> {
  static constexpr auto value = glz::object("app_name", &CtRootData::app_name, "info", &CtRootData::info);
};

template <>
struct glz::meta<CtRootData::Inner> {
  static constexpr auto value = glz::object("version", &CtRootData::Inner::version);
};

struct CtMapData {
  std::string host{"localhost"};
  int port{8080};
};

template <>
struct glz::meta<CtMapData> {
  static constexpr auto value = glz::object("host", &CtMapData::host, "port", &CtMapData::port);
};

struct CtMapWrapper {
  CtMapData config;
};

template <>
struct glz::meta<CtMapWrapper> {
  static constexpr auto value = glz::object("config", &CtMapWrapper::config);
};

// ---- リテラル ----

TEST_CASE("ct_literal", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("hello world");
  auto r = injamm::render<tmpl>(CtBoolData{true});
  REQUIRE(r.has_value());
  REQUIRE(*r == "hello world");
}

// ---- 変数展開 ----

TEST_CASE("ct_var_string", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{name}}");
  auto r = injamm::render<tmpl>(CtUser{"alice", 30});
  REQUIRE(r.has_value());
  REQUIRE(*r == "alice");
}

TEST_CASE("ct_var_int", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{age}}");
  auto r = injamm::render<tmpl>(CtUser{"alice", 30});
  REQUIRE(r.has_value());
  REQUIRE(*r == "30");
}

TEST_CASE("ct_multiple_vars", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{name}}:{{age}}");
  auto r = injamm::render<tmpl>(CtUser{"alice", 30});
  REQUIRE(r.has_value());
  REQUIRE(*r == "alice:30");
}

TEST_CASE("ct_var_mixed_literal", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("Hello {{name}}! You are {{age}}.");
  auto r = injamm::render<tmpl>(CtUser{"Bob", 25});
  REQUIRE(r.has_value());
  REQUIRE(*r == "Hello Bob! You are 25.");
}

// ---- HTML エスケープ ----

TEST_CASE("ct_escaped_output", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{name}}");
  auto r = injamm::render<tmpl>(CtUser{"<b>alice</b>", 30});
  REQUIRE(r.has_value());
  REQUIRE(*r == "&lt;b&gt;alice&lt;/b&gt;");
}

TEST_CASE("ct_raw_output", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{{name}}}");
  auto r = injamm::render<tmpl>(CtUser{"<b>alice</b>", 30});
  REQUIRE(r.has_value());
  REQUIRE(*r == "<b>alice</b>");
}

// ---- {{this}} ----

TEST_CASE("ct_this", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{this}}");
  auto r = injamm::render<tmpl>(CtUser{"alice", 30});
  REQUIRE(r.has_value());
  // Struct {{this}} serializes as JSON, HTML-escaped
  REQUIRE(*r == "{&quot;name&quot;:&quot;alice&quot;,&quot;age&quot;:30}");
}

TEST_CASE("ct_this_struct", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("[{{this}}]");
  auto r = injamm::render<tmpl>(CtUser{"alice", 30});
  REQUIRE(r.has_value());
  REQUIRE(*r == "[{&quot;name&quot;:&quot;alice&quot;,&quot;age&quot;:30}]");
}

// ---- セクション ----

TEST_CASE("ct_section", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{#users}}{{name}}-{{age}}/{{/users}}");
  CtUsersData data;
  data.users.push_back(CtUser{"alice", 30});
  data.users.push_back(CtUser{"bob", 25});
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "alice-30/bob-25/");
}

TEST_CASE("ct_section_empty", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{#users}}{{name}}{{/users}}none");
  CtUsersData data;
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "none");
}

TEST_CASE("ct_section_bool_true", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{#flag}}yes{{/flag}}");
  auto r = injamm::render<tmpl>(CtBoolData{true});
  REQUIRE(r.has_value());
  REQUIRE(*r == "yes");
}

TEST_CASE("ct_section_bool_false", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{#flag}}yes{{/flag}}");
  auto r = injamm::render<tmpl>(CtBoolData{false});
  REQUIRE(r.has_value());
  REQUIRE(*r == "");
}

// ---- 逆セクション ----

TEST_CASE("ct_inverted_true", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{^flag}}no{{/flag}}");
  auto r = injamm::render<tmpl>(CtBoolData{true});
  REQUIRE(r.has_value());
  REQUIRE(*r == "");
}

TEST_CASE("ct_inverted_false", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{^flag}}no{{/flag}}");
  auto r = injamm::render<tmpl>(CtBoolData{false});
  REQUIRE(r.has_value());
  REQUIRE(*r == "no");
}

TEST_CASE("ct_inverted_string_nonempty", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{^name}}empty{{/name}}");
  auto r = injamm::render<tmpl>(CtUser{"Alice", 30});
  REQUIRE(r.has_value());
  REQUIRE(*r == "");
}

TEST_CASE("ct_inverted_string_empty", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{^name}}empty{{/name}}");
  auto r = injamm::render<tmpl>(CtUser{"", 30});
  REQUIRE(r.has_value());
  REQUIRE(*r == "empty");
}

TEST_CASE("ct_inverted_int_nonzero", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{^age}}zero{{/age}}");
  auto r = injamm::render<tmpl>(CtUser{"Alice", 30});
  REQUIRE(r.has_value());
  REQUIRE(*r == "");
}

TEST_CASE("ct_inverted_int_zero", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{^age}}zero{{/age}}");
  auto r = injamm::render<tmpl>(CtUser{"Alice", 0});
  REQUIRE(r.has_value());
  REQUIRE(*r == "zero");
}

// ---- @変数 ----

TEST_CASE("ct_at_index", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{#users}}{{loop.index}}{{/users}}");
  CtUsersData data;
  data.users.push_back(CtUser{"a", 1});
  data.users.push_back(CtUser{"b", 2});
  data.users.push_back(CtUser{"c", 3});
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "012");
}

TEST_CASE("ct_at_index1", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{#users}}{{loop.index1}}:{{name}} {{/users}}");
  CtUsersData data;
  data.users.push_back(CtUser{"a", 1});
  data.users.push_back(CtUser{"b", 2});
  data.users.push_back(CtUser{"c", 3});
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "1:a 2:b 3:c ");
}

TEST_CASE("ct_at_index1_outside_loop", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("X{{loop.index1}}Y");
  CtUsersData data;
  data.users.push_back(CtUser{"a", 1});
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "XY");
}

TEST_CASE("ct_at_first", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{#users}}{{loop.is_first}}{{/users}}");
  CtUsersData data;
  data.users.push_back(CtUser{"a", 1});
  data.users.push_back(CtUser{"b", 2});
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "truefalse");
}

TEST_CASE("ct_at_last", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{#users}}{{loop.is_last}}{{/users}}");
  CtUsersData data;
  data.users.push_back(CtUser{"a", 1});
  data.users.push_back(CtUser{"b", 2});
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "falsetrue");
}

TEST_CASE("ct_at_key_array", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{#users}}{{loop.key}}:{{name}},{{/users}}");
  CtUsersData data;
  data.users.push_back(CtUser{"a", 1});
  data.users.push_back(CtUser{"b", 2});
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "0:a,1:b,");
}

TEST_CASE("ct_at_key_struct", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{#config}}{{loop.key}}={{this}};{{/config}}");
  CtMapWrapper data;
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "host=localhost;port=8080;");
}

// ---- @var セクション ----

TEST_CASE("ct_at_first_section", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{#users}}{{#loop.is_first}}<{{name}}>{{/loop.is_first}}{{/users}}");
  CtUsersData data;
  data.users.push_back(CtUser{"a", 1});
  data.users.push_back(CtUser{"b", 2});
  data.users.push_back(CtUser{"c", 3});
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "<a>");
}

TEST_CASE("ct_at_last_section", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{#users}}{{#loop.is_last}}<{{name}}>{{/loop.is_last}}{{/users}}");
  CtUsersData data;
  data.users.push_back(CtUser{"a", 1});
  data.users.push_back(CtUser{"b", 2});
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "<b>");
}

TEST_CASE("ct_at_first_inverted", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{#users}}{{^loop.is_first}}<{{name}}>{{/loop.is_first}}{{/users}}");
  CtUsersData data;
  data.users.push_back(CtUser{"a", 1});
  data.users.push_back(CtUser{"b", 2});
  data.users.push_back(CtUser{"c", 3});
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "<b><c>");
}

TEST_CASE("ct_at_last_inverted", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{#users}}{{^loop.is_last}}<{{name}}>{{/loop.is_last}}{{/users}}");
  CtUsersData data;
  data.users.push_back(CtUser{"a", 1});
  data.users.push_back(CtUser{"b", 2});
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "<a>");
}

TEST_CASE("ct_at_index_inverted", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{#users}}{{^loop.index}}<{{name}}>{{/loop.index}}{{/users}}");
  CtUsersData data;
  data.users.push_back(CtUser{"a", 1});
  data.users.push_back(CtUser{"b", 2});
  data.users.push_back(CtUser{"c", 3});
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "<a>");
}

// ---- ネストセクション ----

TEST_CASE("ct_nested_section", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{#items}}{{inner}}{{/items}}");
  CtOuter data;
  data.items.push_back(CtNested{"x"});
  data.items.push_back(CtNested{"y"});
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "xy");
}

// ---- ネストパス ----

TEST_CASE("ct_nested_path_simple", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{founder.name}}");
  CtCompany data{.name = "Acme", .founder = CtFounder{.name = "John", .address = {"NYC", "USA"}}};
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "John");
}

TEST_CASE("ct_nested_path_deep", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{founder.address.city}}");
  CtCompany data{.name = "Acme", .founder = CtFounder{.name = "John", .address = {"NYC", "USA"}}};
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "NYC");
}

// ---- @root ----

TEST_CASE("ct_at_root_field_simple", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{root.app_name}}");
  CtRootData data;
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "injamm");
}

TEST_CASE("ct_at_root_field_nested", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{root.info.version}}");
  CtRootData data;
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "1.0");
}

// ---- if/else ----

TEST_CASE("ct_if_true", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{#if age}}adult{{/if}}");
  auto r = injamm::render<tmpl>(CtIfData{"alice", 20});
  REQUIRE(r.has_value());
  REQUIRE(*r == "adult");
}

TEST_CASE("ct_if_false", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{#if age}}adult{{/if}}");
  auto r = injamm::render<tmpl>(CtIfData{"alice", 0});
  REQUIRE(r.has_value());
  REQUIRE(*r == "");
}

TEST_CASE("ct_if_else_true", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{#if age}}A{{else}}B{{/if}}");
  auto r = injamm::render<tmpl>(CtIfData{"alice", 20});
  REQUIRE(r.has_value());
  REQUIRE(*r == "A");
}

TEST_CASE("ct_if_else_false", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{#if age}}A{{else}}B{{/if}}");
  auto r = injamm::render<tmpl>(CtIfData{"alice", 0});
  REQUIRE(r.has_value());
  REQUIRE(*r == "B");
}

TEST_CASE("ct_if_with_at_last", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{#users}}{{name}}{{#if loop.is_last}}.{{/if}}{{/users}}");
  auto r = injamm::render<tmpl>(CtUsersData{.users = {{"a", 1}, {"b", 2}}});
  REQUIRE(r.has_value());
  REQUIRE(*r == "ab.");
}

TEST_CASE("ct_if_else_with_section", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{#users}}{{name}}{{#if loop.is_last}}.{{else}},{{/if}}{{/users}}");
  auto r = injamm::render<tmpl>(CtUsersData{.users = {{"a", 1}, {"b", 2}, {"c", 3}}});
  REQUIRE(r.has_value());
  REQUIRE(*r == "a,b,c.");
}

// ---- std::optional ----

TEST_CASE("ct_optional_present", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{#opt_str}}yes{{/opt_str}}");
  auto r = injamm::render<tmpl>(CtOptionalData{std::optional<std::string>{"hello"}});
  REQUIRE(r.has_value());
  REQUIRE(*r == "yes");
}

TEST_CASE("ct_optional_empty", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{#opt_str}}yes{{/opt_str}}");
  auto r = injamm::render<tmpl>(CtOptionalData{std::nullopt});
  REQUIRE(r.has_value());
  REQUIRE(*r == "");
}

TEST_CASE("ct_optional_var", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{opt_str}}");
  auto r = injamm::render<tmpl>(CtOptionalData{std::optional<std::string>{"hello"}});
  REQUIRE(r.has_value());
  REQUIRE(*r == "hello");
}

// ---- 文字列フィルタ ----

TEST_CASE("ct_filter_upper", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{name | upper}}");
  auto r = injamm::render<tmpl>(CtUser{"hello", 0});
  REQUIRE(r.has_value());
  REQUIRE(*r == "HELLO");
}

TEST_CASE("ct_filter_lower", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{name | lower}}");
  auto r = injamm::render<tmpl>(CtUser{"HELLO", 0});
  REQUIRE(r.has_value());
  REQUIRE(*r == "hello");
}

TEST_CASE("ct_filter_capitalize", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{name | capitalize}}");
  auto r = injamm::render<tmpl>(CtUser{"hello world", 0});
  REQUIRE(r.has_value());
  REQUIRE(*r == "Hello world");
}

TEST_CASE("ct_filter_title", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{name | title}}");
  auto r = injamm::render<tmpl>(CtUser{"hello world", 0});
  REQUIRE(r.has_value());
  REQUIRE(*r == "Hello World");
}

TEST_CASE("ct_filter_trim", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{name | trim}}");
  auto r = injamm::render<tmpl>(CtUser{"  hello  ", 0});
  REQUIRE(r.has_value());
  REQUIRE(*r == "hello");
}

TEST_CASE("ct_filter_left", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{name | left(5)}}");
  auto r = injamm::render<tmpl>(CtUser{"hi", 0});
  REQUIRE(r.has_value());
  REQUIRE(*r == "   hi");
}

TEST_CASE("ct_filter_right", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{name | right(5)}}");
  auto r = injamm::render<tmpl>(CtUser{"hi", 0});
  REQUIRE(r.has_value());
  REQUIRE(*r == "hi   ");
}

TEST_CASE("ct_filter_truncate", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{name | truncate(8)}}");
  auto r = injamm::render<tmpl>(CtUser{"hello world", 0});
  REQUIRE(r.has_value());
  REQUIRE(*r == "hello...");
}

TEST_CASE("ct_filter_substr", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{name | substr(1,3)}}");
  auto r = injamm::render<tmpl>(CtUser{"hello", 0});
  REQUIRE(r.has_value());
  REQUIRE(*r == "ell");
}

TEST_CASE("ct_filter_chaining", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{name | trim | upper}}");
  auto r = injamm::render<tmpl>(CtUser{"  hello  ", 0});
  REQUIRE(r.has_value());
  REQUIRE(*r == "HELLO");
}

TEST_CASE("ct_filter_raw_output", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{{name | trim}}}");
  auto r = injamm::render<tmpl>(CtUser{"  <script>  ", 0});
  REQUIRE(r.has_value());
  REQUIRE(*r == "<script>");
}

TEST_CASE("ct_filter_replace_newlines", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{name | replace}}");
  CtUser data{"line1\nline2\nline3", 0};
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "line1 line2 line3");
}

TEST_CASE("ct_filter_replace_args", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{name | replace(world, injamm)}}");
  CtUser data{"hello world!", 0};
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "hello injamm!");
}

// ---- 整数フィルタ ----

TEST_CASE("ct_int_filter_abs_pos", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{age | abs}}");
  auto r = injamm::render<tmpl>(CtIfData{"t", 42});
  REQUIRE(r.has_value());
  REQUIRE(*r == "42");
}

TEST_CASE("ct_int_filter_abs_neg", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{age | abs}}");
  auto r = injamm::render<tmpl>(CtIfData{"t", -42});
  REQUIRE(r.has_value());
  REQUIRE(*r == "42");
}

TEST_CASE("ct_int_filter_hex", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{age | hex}}");
  auto r = injamm::render<tmpl>(CtIfData{"t", 255});
  REQUIRE(r.has_value());
  REQUIRE(*r == "ff");
}

TEST_CASE("ct_int_filter_bin", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{age | bin}}");
  auto r = injamm::render<tmpl>(CtIfData{"t", 10});
  REQUIRE(r.has_value());
  REQUIRE(*r == "1010");
}

TEST_CASE("ct_int_filter_mod", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{age | mod(4)}}");
  auto r = injamm::render<tmpl>(CtIfData{"t", 10});
  REQUIRE(r.has_value());
  REQUIRE(*r == "2");
}

TEST_CASE("ct_int_filter_mod_by_zero", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{age | mod(0)}}");
  auto r = injamm::render<tmpl>(CtIfData{"t", 17});
  REQUIRE(!r.has_value());
  REQUIRE(r.error().ec == injamm::error_code::division_by_zero);
}

TEST_CASE("ct_int_filter_numify", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{age | numify}}");
  auto r = injamm::render<tmpl>(CtIfData{"t", 1234567});
  REQUIRE(r.has_value());
  REQUIRE(*r == "1,234,567");
}

TEST_CASE("ct_int_filter_zerofill", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{age | zerofill(5)}}");
  auto r = injamm::render<tmpl>(CtIfData{"t", 42});
  REQUIRE(r.has_value());
  REQUIRE(*r == "00042");
}

TEST_CASE("ct_int_filter_zerofill_exact", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{age | zerofill(3)}}");
  auto r = injamm::render<tmpl>(CtIfData{"t", 123});
  REQUIRE(r.has_value());
  REQUIRE(*r == "123");
}

TEST_CASE("ct_int_filter_zerofill_negative", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{age | zerofill(5)}}");
  auto r = injamm::render<tmpl>(CtIfData{"t", -42});
  REQUIRE(r.has_value());
  REQUIRE(*r == "-0042");
}

TEST_CASE("ct_int_filter_zerofill_zero", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{age | zerofill(4)}}");
  auto r = injamm::render<tmpl>(CtIfData{"t", 0});
  REQUIRE(r.has_value());
  REQUIRE(*r == "0000");
}

// ---- LLONG_MIN UB 回避テスト ----

TEST_CASE("ct_int_filter_abs LLONG_MIN", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{val | abs}}");
  auto r = injamm::render<tmpl>(CtLlData{LLONG_MIN});
  REQUIRE(r.has_value());
  REQUIRE(*r == "9223372036854775808");
}

TEST_CASE("ct_int_filter_neg LLONG_MIN", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{val | neg}}");
  auto r = injamm::render<tmpl>(CtLlData{LLONG_MIN});
  REQUIRE(r.has_value());
  REQUIRE(*r == "9223372036854775808");
}

TEST_CASE("ct_int_filter_numify LLONG_MIN", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{val | numify}}");
  auto r = injamm::render<tmpl>(CtLlData{LLONG_MIN});
  REQUIRE(r.has_value());
  REQUIRE(*r == "-9,223,372,036,854,775,808");
}

TEST_CASE("ct_int_filter_zerofill LLONG_MIN", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{val | zerofill(25)}}");
  auto r = injamm::render<tmpl>(CtLlData{LLONG_MIN});
  REQUIRE(r.has_value());
  REQUIRE(*r == "-000009223372036854775808");
}

// ---- 実数フィルタ ----

TEST_CASE("ct_float_filter_precision", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{value | precision(2)}}");
  auto r = injamm::render<tmpl>(CtFloatData{3.14159});
  REQUIRE(r.has_value());
  REQUIRE(*r == "3.14");
}

// ---- if + フィルタ ----

TEST_CASE("ct_if_filter_is_neg", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{#if age | is_neg}}neg{{else}}pos{{/if}}");
  auto r = injamm::render<tmpl>(CtIfData{"t", -5});
  REQUIRE(r.has_value());
  REQUIRE(*r == "neg");
}

TEST_CASE("ct_if_filter_eq", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{#if age | eq(25)}}yes{{/if}}");
  auto r = injamm::render<tmpl>(CtIfData{"t", 25});
  REQUIRE(r.has_value());
  REQUIRE(*r == "yes");
}

TEST_CASE("ct_if_filter_chain", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{#if age | mod(4) | eq(2)}}yes{{/if}}");
  auto r = injamm::render<tmpl>(CtIfData{"t", 10});
  REQUIRE(r.has_value());
  REQUIRE(*r == "yes");
}

// ---- 比較フィルタ CT テスト ----

TEST_CASE("ct_int_filter_ne", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{age | ne(10)}}");
  auto r = injamm::render<tmpl>(CtIfData{"t", 42});
  REQUIRE(r.has_value());
  REQUIRE(*r == "true");
}

TEST_CASE("ct_int_filter_gt", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{age | gt(18)}}");
  auto r = injamm::render<tmpl>(CtIfData{"t", 25});
  REQUIRE(r.has_value());
  REQUIRE(*r == "true");
}

TEST_CASE("ct_int_filter_gte_equal", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{age | gte(18)}}");
  auto r = injamm::render<tmpl>(CtIfData{"t", 18});
  REQUIRE(r.has_value());
  REQUIRE(*r == "true");
}

TEST_CASE("ct_int_filter_lt", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{age | lt(10)}}");
  auto r = injamm::render<tmpl>(CtIfData{"t", 5});
  REQUIRE(r.has_value());
  REQUIRE(*r == "true");
}

TEST_CASE("ct_int_filter_lte_equal", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{age | lte(10)}}");
  auto r = injamm::render<tmpl>(CtIfData{"t", 10});
  REQUIRE(r.has_value());
  REQUIRE(*r == "true");
}

TEST_CASE("ct_int_filter_float_ne", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{value | ne(3)}}");
  auto r = injamm::render<tmpl>(CtFloatData{2.5});
  REQUIRE(r.has_value());
  REQUIRE(*r == "true");
}

TEST_CASE("ct_int_filter_gt_float", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{value | gt(3)}}");
  auto r = injamm::render<tmpl>(CtFloatData{3.14});
  REQUIRE(r.has_value());
  REQUIRE(*r == "true");
}

TEST_CASE("ct_int_filter_parse_failure_false", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{name | gt(0)}}");
  auto r = injamm::render<tmpl>(CtIfData{"hello", 42});
  REQUIRE(r.has_value());
  REQUIRE(*r == "false");
}

TEST_CASE("ct_if_filter_gt", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{#if age | gt(18)}}adult{{else}}minor{{/if}}");
  auto r = injamm::render<tmpl>(CtIfData{"t", 25});
  REQUIRE(r.has_value());
  REQUIRE(*r == "adult");
}

TEST_CASE("ct_if_filter_lte", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{#if age | lte(10)}}small{{/if}}");
  auto r = injamm::render<tmpl>(CtIfData{"t", 5});
  REQUIRE(r.has_value());
  REQUIRE(*r == "small");
}

TEST_CASE("ct_if_filter_chain_ne", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{#if age | mod(4) | ne(0)}}not_divisible{{/if}}");
  auto r = injamm::render<tmpl>(CtIfData{"t", 5});
  REQUIRE(r.has_value());
  REQUIRE(*r == "not_divisible");
}

// ---- break / continue ----

TEST_CASE("ct_break", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{#users}}{{name}}{{#if loop.is_last}}.{{else}}{{#break}}{{/if}}{{/users}}");
  CtUsersData data;
  data.users.push_back(CtUser{"a", 1});
  data.users.push_back(CtUser{"b", 2});
  data.users.push_back(CtUser{"c", 3});
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "a");
}

TEST_CASE("ct_continue", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{#users}}{{#if loop.index}}{{#continue}}{{/if}}{{name}}{{/users}}");
  CtUsersData data;
  data.users.push_back(CtUser{"a", 1});
  data.users.push_back(CtUser{"b", 2});
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "a");
}

// ---- ネスト if/section ----

TEST_CASE("ct_nested_if_section", "[injamm][ct]") {
  auto constexpr tmpl =
      injamm::fixed_string("{{#if users}}{{#users}}{{name}},{{/users}}{{/if}}");
  CtUsersData data;
  data.users.push_back(CtUser{"alice", 30});
  data.users.push_back(CtUser{"bob", 25});
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "alice,bob,");
}

// ---- 構造体イテレーション ----

TEST_CASE("ct_struct_iteration", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{#config}}{{loop.key}}={{this}};{{/config}}");
  CtMapWrapper data;
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  // Iterates over config's field members
  REQUIRE(*r == "host=localhost;port=8080;");
}

// ---- エラーケース ----

TEST_CASE("ct_unknown_key", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{nonexistent}}");
  auto r = injamm::render<tmpl>(CtBoolData{true});
  REQUIRE(!r.has_value());
  REQUIRE(r.error().ec == injamm::error_code::unknown_key);
}

// ---- std::map テスト用データ型 ----

struct CtMapIntData {
  std::map<std::string, int> values;
};

template <>
struct glz::meta<CtMapIntData> {
  static constexpr auto value = glz::object("values", &CtMapIntData::values);
};

struct CtMapStrData {
  std::map<std::string, std::string> labels;
};

template <>
struct glz::meta<CtMapStrData> {
  static constexpr auto value = glz::object("labels", &CtMapStrData::labels);
};

struct CtMapItem {
  std::string name;
  int score{};
};

template <>
struct glz::meta<CtMapItem> {
  static constexpr auto value = glz::object("name", &CtMapItem::name, "score", &CtMapItem::score);
};

struct CtMapStructData {
  std::map<std::string, CtMapItem> items;
};

template <>
struct glz::meta<CtMapStructData> {
  static constexpr auto value = glz::object("items", &CtMapStructData::items);
};

// ---- std::map CT レンダリング テスト ----

TEST_CASE("ct_map_section_basic", "[injamm][ct][map]") {
  auto constexpr tmpl = injamm::fixed_string("{{#values}}{{loop.key}}={{this}} {{/values}}");
  CtMapIntData data{{ {"a", 1}, {"b", 2}, {"c", 3} }};
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "a=1 b=2 c=3 ");
}

TEST_CASE("ct_map_section_string_values", "[injamm][ct][map]") {
  auto constexpr tmpl = injamm::fixed_string("{{#labels}}{{loop.key}}:{{this}} {{/labels}}");
  CtMapStrData data{{ {"color", "red"}, {"size", "large"} }};
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "color:red size:large ");
}

TEST_CASE("ct_map_section_struct_values", "[injamm][ct][map]") {
  auto constexpr tmpl = injamm::fixed_string("{{#items}}{{loop.key}}:{{name}}={{score}} {{/items}}");
  CtMapStructData data{{ {"alice", {.name = "Alice", .score = 100}}, {"bob", {.name = "Bob", .score = 85}} }};
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "alice:Alice=100 bob:Bob=85 ");
}

TEST_CASE("ct_map_section_empty", "[injamm][ct][map]") {
  auto constexpr tmpl = injamm::fixed_string("before{{#values}}NEVER{{/values}}after");
  CtMapIntData data;
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "beforeafter");
}

TEST_CASE("ct_map_inverted_empty", "[injamm][ct][map]") {
  auto constexpr tmpl = injamm::fixed_string("{{^values}}empty{{/values}}");
  CtMapIntData data;
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "empty");
}

TEST_CASE("ct_map_inverted_nonempty", "[injamm][ct][map]") {
  auto constexpr tmpl = injamm::fixed_string("{{^values}}empty{{/values}}");
  CtMapIntData data{{ {"x", 1} }};
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "");
}

TEST_CASE("ct_map_single_entry", "[injamm][ct][map]") {
  auto constexpr tmpl = injamm::fixed_string("{{#values}}{{loop.key}}={{this}}{{/values}}");
  CtMapIntData data{{ {"only", 99} }};
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "only=99");
}

// ---- std::set テスト ----

struct CtSetIntData {
  std::set<int> values;
};

template <>
struct glz::meta<CtSetIntData> {
  static constexpr auto value = glz::object("values", &CtSetIntData::values);
};

TEST_CASE("ct_set_section", "[injamm][ct][set]") {
  auto constexpr tmpl = injamm::fixed_string("{{#values}}[{{this}}]{{/values}}");
  CtSetIntData data{{ {3, 1, 2} }};
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "[1][2][3]");
}

TEST_CASE("ct_set_empty", "[injamm][ct][set]") {
  auto constexpr tmpl = injamm::fixed_string("{{#values}}[{{this}}]{{/values}}");
  CtSetIntData data{};
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "");
}

TEST_CASE("ct_set_inverted_empty", "[injamm][ct][set]") {
  auto constexpr tmpl = injamm::fixed_string("{{^values}}empty{{/values}}");
  CtSetIntData data{};
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "empty");
}

TEST_CASE("ct_set_inverted_nonempty", "[injamm][ct][set]") {
  auto constexpr tmpl = injamm::fixed_string("{{^values}}empty{{/values}}");
  CtSetIntData data{{ {1} }};
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "");
}

TEST_CASE("ct_set_if", "[injamm][ct][set]") {
  auto constexpr tmpl = injamm::fixed_string("{{#values}}[{{this}}]{{/values}}");
  CtSetIntData data{{ {1, 2} }};
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "[1][2]");
}

TEST_CASE("ct_set_if_empty", "[injamm][ct][set]") {
  auto constexpr tmpl = injamm::fixed_string("{{#values}}[{{this}}]{{/values}}");
  CtSetIntData data{};
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "");
}

// ---- @var NTTP expansion ----

// ---- @var NTTP expansion 用データ型 ----

struct CtAtVarItem {
  std::string val;
};

template <>
struct glz::meta<CtAtVarItem> {
  static constexpr auto value = glz::object("val", &CtAtVarItem::val);
};

struct CtAtVarItemsCtx {
  std::vector<CtAtVarItem> items;
};

template <>
struct glz::meta<CtAtVarItemsCtx> {
  static constexpr auto value = glz::object("items", &CtAtVarItemsCtx::items);
};

// ---- @var NTTP expansion ----

TEST_CASE("@var basic expansion in render (NTTP)", "[injamm][ct][atvar]") {
  CtUser ctx{"Alice", 30};
  auto result = injamm::render<"Hello {{@var(f)}}!", "f", "name">(ctx);
  REQUIRE(result.has_value());
  CHECK(*result == "Hello Alice!");
}

TEST_CASE("@var with raw tag (NTTP)", "[injamm][ct][atvar]") {
  CtUser ctx{"Alice", 30};
  auto result = injamm::render<"{{{@var(f)}}}", "f", "name">(ctx);
  REQUIRE(result.has_value());
  CHECK(*result == "Alice");
}

TEST_CASE("@var in section with NTTP", "[injamm][ct][atvar]") {
  CtAtVarItemsCtx ctx{{{"A"}, {"B"}}};
  auto result = injamm::render<"{{#@var(s)}}{{val}}{{/@var(s)}}", "s", "items">(ctx);
  REQUIRE(result.has_value());
  CHECK(*result == "AB");
}

TEST_CASE("@var with filter (NTTP)", "[injamm][ct][atvar]") {
  CtUser ctx{"alice", 30};
  auto result = injamm::render<"{{@var(f) | upper}}", "f", "name">(ctx);
  REQUIRE(result.has_value());
  CHECK(*result == "ALICE");
}

TEST_CASE("ct_comment_basic", "[injamm][ct][comment]") {
  CtUser user{"Alice", 30};
  auto r = injamm::render<"Hello {# this is a comment #}{{name}}!">(user);
  REQUIRE(r.has_value());
  CHECK(*r == "Hello Alice!");
}

TEST_CASE("ct_comment_multiline", "[injamm][ct][comment]") {
  CtUser user{"Alice", 30};
  auto r = injamm::render<"Hello {# multi\nline\ncomment #}{{name}}!">(user);
  REQUIRE(r.has_value());
  CHECK(*r == "Hello Alice!");
}

TEST_CASE("ct_comment_between_literals", "[injamm][ct][comment]") {
  CtUser user{"Alice", 30};
  auto r = injamm::render<"Hello {# comment #}{{name}}!">(user);
  REQUIRE(r.has_value());
  CHECK(*r == "Hello Alice!");
}

TEST_CASE("ct_comment_in_section", "[injamm][ct][comment]") {
  CtUsersData data{{{"Alice", 30}, {"Bob", 25}}};
  auto r = injamm::render<"{{#users}}{# comment #}{{name}}{{/users}}">(data);
  REQUIRE(r.has_value());
  CHECK(*r == "AliceBob");
}

TEST_CASE("ct_comment_in_if_body", "[injamm][ct][comment]") {
  CtIfData data{"test", 25};
  auto r = injamm::render<"{{#if age}}{# age is nonzero #}yes{{/if}}">(data);
  REQUIRE(r.has_value());
  CHECK(*r == "yes");
}

TEST_CASE("ct_comment_ignore_inner_tags", "[injamm][ct][comment]") {
  CtUser user{"Alice", 30};
  auto r = injamm::render<"{{name}}{# {{age}} should be ignored #}!">(user);
  REQUIRE(r.has_value());
  CHECK(*r == "Alice!");
}

TEST_CASE("ct_comment_multiple", "[injamm][ct][comment]") {
  CtUser user{"Alice", 30};
  auto r = injamm::render<"{#c1#}before{{name}}{#c2#}after">(user);
  REQUIRE(r.has_value());
  CHECK(*r == "beforeAliceafter");
}

// ---- trim_blocks / lstrip_blocks tests (CT) ----

TEST_CASE("ct_trim_blocks removes newline after }}", "[injamm][ct][whitespace]") {
  CtUser user{"Alice", 30};
  auto r = injamm::render<"a{{name}}\nb", true>(user);
  REQUIRE(r.has_value());
  CHECK(*r == "aAliceb");
}

TEST_CASE("ct_trim_blocks does nothing when no newline follows", "[injamm][ct][whitespace]") {
  CtUser user{"Alice", 30};
  auto r = injamm::render<"{{name}}{{age}}", true>(user);
  REQUIRE(r.has_value());
  CHECK(*r == "Alice30");
}

TEST_CASE("ct_trim_blocks with section open/close", "[injamm][ct][whitespace]") {
  CtBoolData data{true};
  auto r = injamm::render<"x{{#flag}}\ny\n{{/flag}}z", true>(data);
  REQUIRE(r.has_value());
  CHECK(*r == "xy\nz");
}

TEST_CASE("ct_lstrip_blocks strips whitespace before section open", "[injamm][ct][whitespace]") {
  CtBoolData data{true};
  auto r = injamm::render<"a\n  {{#flag}}y{{/flag}}", false, true>(data);
  REQUIRE(r.has_value());
  CHECK(*r == "a\ny");
}

TEST_CASE("ct_lstrip_blocks strips whitespace before section close", "[injamm][ct][whitespace]") {
  CtBoolData data{true};
  auto r = injamm::render<"{{#flag}}y\n  {{/flag}}", false, true>(data);
  REQUIRE(r.has_value());
  CHECK(*r == "y\n");
}

TEST_CASE("ct_lstrip_blocks does not affect expression tags", "[injamm][ct][whitespace]") {
  CtUser user{"Alice", 30};
  auto r = injamm::render<"  {{name}}  {{age}}", false, true>(user);
  REQUIRE(r.has_value());
  CHECK(*r == "  Alice  30");
}

TEST_CASE("ct_trim_lstrip_blocks combined", "[injamm][ct][whitespace]") {
  CtBoolData data{true};
  auto r = injamm::render<"{{#flag}}\n  y\n{{/flag}}", true, true>(data);
  REQUIRE(r.has_value());
  CHECK(*r == "  y\n");
}

// ---- FrozenString NTTP テスト（frozenchars 利用時のみ）----

#ifdef INJAMM_HAS_FROZENCHARS
#include <frozenchars/literals.hpp>

TEST_CASE("FrozenString basic placeholder (constexpr variable)", "[injamm][ct][frozen]") {
  using namespace frozenchars::literals;
  auto constexpr tmpl = "{{name}}"_fs;
  CtUser user{"alice", 30};
  auto r = injamm::render<tmpl>(user);
  REQUIRE(r.has_value());
  CHECK(*r == "alice");
}

TEST_CASE("FrozenString literal NTTP", "[injamm][ct][frozen]") {
  using namespace frozenchars::literals;
  auto constexpr tmpl = "Hello {{name}}! You are {{age}}."_fs;
  CtUser user{"Bob", 25};
  auto r = injamm::render<tmpl>(user);
  REQUIRE(r.has_value());
  CHECK(*r == "Hello Bob! You are 25.");
}

TEST_CASE("FrozenString HTML escape", "[injamm][ct][frozen]") {
  using namespace frozenchars::literals;
  auto constexpr tmpl = "{{name}}"_fs;
  CtUser user{"<b>alice</b>", 30};
  auto r = injamm::render<tmpl>(user);
  REQUIRE(r.has_value());
  CHECK(*r == "&lt;b&gt;alice&lt;/b&gt;");
}

TEST_CASE("FrozenString raw output {{{var}}}", "[injamm][ct][frozen]") {
  using namespace frozenchars::literals;
  auto constexpr tmpl = "{{{name}}}"_fs;
  CtUser user{"<b>alice</b>", 30};
  auto r = injamm::render<tmpl>(user);
  REQUIRE(r.has_value());
  CHECK(*r == "<b>alice</b>");
}

TEST_CASE("FrozenString section", "[injamm][ct][frozen]") {
  using namespace frozenchars::literals;
  auto constexpr tmpl = "{{#users}}{{name}}-{{age}}/{{/users}}"_fs;
  CtUsersData data{{{"a", 1}, {"b", 2}}};
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  CHECK(*r == "a-1/b-2/");
}

TEST_CASE("FrozenString inverted section", "[injamm][ct][frozen]") {
  using namespace frozenchars::literals;
  auto constexpr tmpl = "{{^flag}}no{{/flag}}"_fs;
  auto r1 = injamm::render<tmpl>(CtBoolData{false});
  REQUIRE(r1.has_value());
  CHECK(*r1 == "no");
  auto r2 = injamm::render<tmpl>(CtBoolData{true});
  REQUIRE(r2.has_value());
  CHECK(*r2 == "");
}

TEST_CASE("FrozenString @index loop", "[injamm][ct][frozen]") {
  using namespace frozenchars::literals;
  auto constexpr tmpl = "{{#users}}{{loop.index}}{{/users}}"_fs;
  CtUsersData data{{{"a", 1}, {"b", 2}}};
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  CHECK(*r == "01");
}

TEST_CASE("FrozenString nested path", "[injamm][ct][frozen]") {
  using namespace frozenchars::literals;
  auto constexpr tmpl = "{{founder.name}}"_fs;
  CtCompany ctx{"Acme", {"Alice", {"NYC", "USA"}}};
  auto r = injamm::render<tmpl>(ctx);
  REQUIRE(r.has_value());
  CHECK(*r == "Alice");
}

TEST_CASE("FrozenString if/else", "[injamm][ct][frozen]") {
  using namespace frozenchars::literals;
  auto constexpr tmpl = "{{#if age}}A{{else}}B{{/if}}"_fs;
  auto r1 = injamm::render<tmpl>(CtIfData{"t", 20});
  REQUIRE(r1.has_value());
  CHECK(*r1 == "A");
  auto r2 = injamm::render<tmpl>(CtIfData{"t", 0});
  REQUIRE(r2.has_value());
  CHECK(*r2 == "B");
}

TEST_CASE("FrozenString @var expansion", "[injamm][ct][frozen][atvar]") {
  using namespace frozenchars::literals;
  auto constexpr tmpl = "Hello {{@var(f)}}!"_fs;
  auto constexpr vk = "f"_fs;
  auto constexpr vv = "name"_fs;
  CtUser user{"alice", 30};
  auto result = injamm::render<tmpl, vk, vv>(user);
  REQUIRE(result.has_value());
  CHECK(*result == "Hello alice!");
}

#endif // INJAMM_HAS_FROZENCHARS

// ---- CT 配列インデックステスト用データ型 ----

struct CtGuest {
  std::string name;
};

struct CtParty {
  std::vector<std::string> guests;
  std::vector<CtGuest> members;
  std::string title;
};

template <>
struct glz::meta<CtGuest> {
  static constexpr auto value = glz::object("name", &CtGuest::name);
};

template <>
struct glz::meta<CtParty> {
  static constexpr auto value = glz::object("guests", &CtParty::guests, "members", &CtParty::members, "title", &CtParty::title);
};

TEST_CASE("ct_array_index_first", "[injamm][ct][array_index]") {
  auto constexpr tmpl = injamm::fixed_string("{{guests.0}}");
  CtParty data{{"Jeff", "Tom", "Patrick"}, {}, "Party"};
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  CHECK(*r == "Jeff");
}

TEST_CASE("ct_array_index_mid", "[injamm][ct][array_index]") {
  auto constexpr tmpl = injamm::fixed_string("{{guests.1}}");
  CtParty data{{"Jeff", "Tom", "Patrick"}, {}, "Party"};
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  CHECK(*r == "Tom");
}

TEST_CASE("ct_array_index_last", "[injamm][ct][array_index]") {
  auto constexpr tmpl = injamm::fixed_string("{{guests.2}}");
  CtParty data{{"Jeff", "Tom", "Patrick"}, {}, "Party"};
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  CHECK(*r == "Patrick");
}

TEST_CASE("ct_array_index_out_of_bounds", "[injamm][ct][array_index]") {
  auto constexpr tmpl = injamm::fixed_string("{{guests.99}}");
  CtParty data{{"Jeff", "Tom", "Patrick"}, {}, "Party"};
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  CHECK(*r == "");
}

TEST_CASE("ct_array_index_nested_field", "[injamm][ct][array_index]") {
  auto constexpr tmpl = injamm::fixed_string("{{members.1.name}}");
  CtParty data{{}, {{"Alice"}, {"Bob"}, {"Charlie"}}, ""};
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  CHECK(*r == "Bob");
}

TEST_CASE("ct_array_index_raw", "[injamm][ct][array_index]") {
  auto constexpr tmpl = injamm::fixed_string("{{{guests.0}}}");
  CtParty data{{"<Jeff>", "<Tom>"}, {}, ""};
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  CHECK(*r == "<Jeff>");
}

TEST_CASE("ct_array_index_with_literal", "[injamm][ct][array_index]") {
  auto constexpr tmpl = injamm::fixed_string("Hello {{guests.1}}!");
  CtParty data{{"Jeff", "Tom", "Patrick"}, {}, "Party"};
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  CHECK(*r == "Hello Tom!");
}

TEST_CASE("ct_array_index_empty_vec", "[injamm][ct][array_index]") {
  auto constexpr tmpl = injamm::fixed_string("{{guests.0}}");
  CtParty data{{}, {}, ""};
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  CHECK(*r == "");
}

TEST_CASE("ct_array_index_nested_deep", "[injamm][ct][array_index]") {
  auto constexpr tmpl = injamm::fixed_string("{{members.0.name}}-{{members.1.name}}");
  CtParty data{{}, {{"Alice"}, {"Bob"}}, ""};
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  CHECK(*r == "Alice-Bob");
}

TEST_CASE("ct_array_index_in_if", "[injamm][ct][array_index]") {
  auto constexpr tmpl = injamm::fixed_string("{{#if guests.1}}has_second{{/if}}");
  CtParty data{{"Jeff", "Tom", "Patrick"}, {}, "Party"};
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  CHECK(*r == "has_second");
}

TEST_CASE("ct_array_index_in_if_empty", "[injamm][ct][array_index]") {
  auto constexpr tmpl = injamm::fixed_string("{{#if guests.0}}has_first{{/if}}");
  CtParty data{{}, {}, ""};
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  CHECK(*r == "");
}

TEST_CASE("ct_trim_blocks single var with no newline", "[injamm][ct][whitespace]") {
  CtUser user{"Alice", 30};
  SECTION("single var only") {
    auto r = injamm::render<"{{name}}", true>(user);
    REQUIRE(r.has_value());
    CHECK(*r == "Alice");
  }
  SECTION("two vars with no newlines, explicit false flags") {
    auto r = injamm::render<"{{name}}{{age}}", false, false>(user);
    REQUIRE(r.has_value());
    CHECK(*r == "Alice30");
  }
  SECTION("two vars with trim_blocks=true") {
    auto r = injamm::render<"{{name}}{{age}}", true>(user);
    REQUIRE(r.has_value());
    CHECK(*r == "Alice30");
  }
  SECTION("two vars with trim_blocks=true, explicit false lstrip") {
    auto r = injamm::render<"{{name}}{{age}}", true, false>(user);
    REQUIRE(r.has_value());
    CHECK(*r == "Alice30");
  }
}

// ---- bind（コンテナ直接バインド）テスト ----

TEST_CASE("CT: bind - vector of strings", "[injamm][ct][bind]") {
  std::vector<std::string> items = {"apple", "banana", "cherry"};
  auto res = injamm::render<"{{#items}}{{this}} {{/items}}">(injamm::bind<"items">(items));
  REQUIRE(res);
  CHECK(*res == "apple banana cherry ");
}

TEST_CASE("CT: bind - vector of structs", "[injamm][ct][bind]") {
  std::vector<CtUser> users = {{"Alice", 30}, {"Bob", 25}};
  auto res = injamm::render<"{{#users}}{{name}}:{{age}} {{/users}}">(injamm::bind<"users">(users));
  REQUIRE(res);
  CHECK(*res == "Alice:30 Bob:25 ");
}

TEST_CASE("CT: bind - map", "[injamm][ct][bind]") {
  std::map<std::string, int> scores = {{"math", 90}, {"english", 85}};
  auto res = injamm::render<"{{#scores}}{{loop.key}}={{this}} {{/scores}}">(injamm::bind<"scores">(scores));
  REQUIRE(res);
  CHECK(*res == "english=85 math=90 ");
}

TEST_CASE("CT: bind - multiple containers", "[injamm][ct][bind]") {
  std::vector<CtUser> users = {{"Hello", 0}};
  std::vector<std::string> items = {"a", "b"};
  auto res = injamm::render<"{{#users}}{{name}}{{/users}}: {{#items}}{{this}} {{/items}}">(
    injamm::bind<"users", "items">(users, items));
  REQUIRE(res);
  CHECK(*res == "Hello: a b ");
}

TEST_CASE("CT: bind - loop.index", "[injamm][ct][bind]") {
  std::vector<int> nums = {10, 20, 30};
  auto res = injamm::render<"{{#nums}}{{loop.index}}={{this}} {{/nums}}">(injamm::bind<"nums">(nums));
  REQUIRE(res);
  CHECK(*res == "0=10 1=20 2=30 ");
}

TEST_CASE("CT: bind - empty vector inverted section", "[injamm][ct][bind]") {
  std::vector<std::string> items;
  auto res = injamm::render<"{{^items}}empty{{/items}}">(injamm::bind<"items">(items));
  REQUIRE(res);
  CHECK(*res == "empty");
}

TEST_CASE("CT: bind - set", "[injamm][ct][bind]") {
  std::set<int> nums = {3, 1, 2};
  auto res = injamm::render<"{{#nums}}{{this}} {{/nums}}">(injamm::bind<"nums">(nums));
  REQUIRE(res);
  CHECK(*res == "1 2 3 ");
}

TEST_CASE("CT: bind - set with loop.is_first/is_last", "[injamm][ct][bind]") {
  std::set<int> nums = {10, 20};
  auto res = injamm::render<"{{#nums}}{{#if loop.is_first}}[{{/if}}{{this}}{{#if loop.is_last}}]{{/if}} {{/nums}}">(
    injamm::bind<"nums">(nums));
  REQUIRE(res);
  CHECK(*res == "[10 20] ");
}

TEST_CASE("CT: bind - loop.is_first/is_last with vector", "[injamm][ct][bind]") {
  std::vector<int> nums = {10, 20, 30};
  auto res = injamm::render<"{{#nums}}{{#if loop.is_first}}FIRST{{/if}}{{this}} {{#if loop.is_last}}LAST{{/if}}{{/nums}}">(
    injamm::bind<"nums">(nums));
  REQUIRE(res);
  CHECK(*res == "FIRST10 20 30 LAST");
}

TEST_CASE("CT: bind - title and items", "[injamm][ct][bind]") {
  std::vector<std::string> items = {"a", "b"};
  std::string title = "Title";
  auto res = injamm::render<"{{title}}: {{#items}}{{this}} {{/items}}">(
    injamm::bind<"title", "items">(title, items));
  REQUIRE(res);
  CHECK(*res == "Title: a b ");
}

TEST_CASE("CT: bind - string scalar", "[injamm][ct][bind]") {
  std::string name = "World";
  auto res = injamm::render<"Hello {{name}}!">(injamm::bind<"name">(name));
  REQUIRE(res);
  CHECK(*res == "Hello World!");
}

TEST_CASE("CT: bind - int scalar", "[injamm][ct][bind]") {
  int count = 42;
  auto res = injamm::render<"Count: {{count}}">(injamm::bind<"count">(count));
  REQUIRE(res);
  CHECK(*res == "Count: 42");
}

TEST_CASE("CT: bind - implicit value", "[injamm][ct][bind]") {
  int val = 99;
  auto res = injamm::render<"Got {{value}}">(injamm::bind(val));
  REQUIRE(res);
  CHECK(*res == "Got 99");
}

TEST_CASE("CT: bind - implicit value string", "[injamm][ct][bind]") {
  std::string val = "hello";
  auto res = injamm::render<"{{value}} world">(injamm::bind(val));
  REQUIRE(res);
  CHECK(*res == "hello world");
}
