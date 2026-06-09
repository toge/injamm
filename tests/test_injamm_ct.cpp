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

// ---- @変数 ----

TEST_CASE("ct_at_index", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{#users}}{{@index}}{{/users}}");
  CtUsersData data;
  data.users.push_back(CtUser{"a", 1});
  data.users.push_back(CtUser{"b", 2});
  data.users.push_back(CtUser{"c", 3});
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "012");
}

TEST_CASE("ct_at_first", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{#users}}{{@first}}{{/users}}");
  CtUsersData data;
  data.users.push_back(CtUser{"a", 1});
  data.users.push_back(CtUser{"b", 2});
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "truefalse");
}

TEST_CASE("ct_at_last", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{#users}}{{@last}}{{/users}}");
  CtUsersData data;
  data.users.push_back(CtUser{"a", 1});
  data.users.push_back(CtUser{"b", 2});
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "falsetrue");
}

TEST_CASE("ct_at_key_array", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{#users}}{{@key}}:{{name}},{{/users}}");
  CtUsersData data;
  data.users.push_back(CtUser{"a", 1});
  data.users.push_back(CtUser{"b", 2});
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "0:a,1:b,");
}

TEST_CASE("ct_at_key_struct", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{#config}}{{@key}}={{this}};{{/config}}");
  CtMapWrapper data;
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "host=localhost;port=8080;");
}

// ---- @var セクション ----

TEST_CASE("ct_at_first_section", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{#users}}{{#@first}}<{{name}}>{{/@first}}{{/users}}");
  CtUsersData data;
  data.users.push_back(CtUser{"a", 1});
  data.users.push_back(CtUser{"b", 2});
  data.users.push_back(CtUser{"c", 3});
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "<a>");
}

TEST_CASE("ct_at_last_section", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{#users}}{{#@last}}<{{name}}>{{/@last}}{{/users}}");
  CtUsersData data;
  data.users.push_back(CtUser{"a", 1});
  data.users.push_back(CtUser{"b", 2});
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "<b>");
}

TEST_CASE("ct_at_first_inverted", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{#users}}{{^@first}}<{{name}}>{{/@first}}{{/users}}");
  CtUsersData data;
  data.users.push_back(CtUser{"a", 1});
  data.users.push_back(CtUser{"b", 2});
  data.users.push_back(CtUser{"c", 3});
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "<b><c>");
}

TEST_CASE("ct_at_last_inverted", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{#users}}{{^@last}}<{{name}}>{{/@last}}{{/users}}");
  CtUsersData data;
  data.users.push_back(CtUser{"a", 1});
  data.users.push_back(CtUser{"b", 2});
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
  auto constexpr tmpl = injamm::fixed_string("{{@root.app_name}}");
  CtRootData data;
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "injamm");
}

TEST_CASE("ct_at_root_field_nested", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{@root.info.version}}");
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
  auto constexpr tmpl = injamm::fixed_string("{{#users}}{{name}}{{#if @last}}.{{/if}}{{/users}}");
  auto r = injamm::render<tmpl>(CtUsersData{.users = {{"a", 1}, {"b", 2}}});
  REQUIRE(r.has_value());
  REQUIRE(*r == "ab.");
}

TEST_CASE("ct_if_else_with_section", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{#users}}{{name}}{{#if @last}}.{{else}},{{/if}}{{/users}}");
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

// ---- break / continue ----

TEST_CASE("ct_break", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{#users}}{{name}}{{#if @last}}.{{else}}{{#break}}{{/if}}{{/users}}");
  CtUsersData data;
  data.users.push_back(CtUser{"a", 1});
  data.users.push_back(CtUser{"b", 2});
  data.users.push_back(CtUser{"c", 3});
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "a");
}

TEST_CASE("ct_continue", "[injamm][ct]") {
  auto constexpr tmpl = injamm::fixed_string("{{#users}}{{#if @index}}{{#continue}}{{/if}}{{name}}{{/users}}");
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
  auto constexpr tmpl = injamm::fixed_string("{{#config}}{{@key}}={{this}};{{/config}}");
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
  auto constexpr tmpl = injamm::fixed_string("{{#values}}{{@key}}={{this}} {{/values}}");
  CtMapIntData data{{ {"a", 1}, {"b", 2}, {"c", 3} }};
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "a=1 b=2 c=3 ");
}

TEST_CASE("ct_map_section_string_values", "[injamm][ct][map]") {
  auto constexpr tmpl = injamm::fixed_string("{{#labels}}{{@key}}:{{this}} {{/labels}}");
  CtMapStrData data{{ {"color", "red"}, {"size", "large"} }};
  auto r = injamm::render<tmpl>(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "color:red size:large ");
}

TEST_CASE("ct_map_section_struct_values", "[injamm][ct][map]") {
  auto constexpr tmpl = injamm::fixed_string("{{#items}}{{@key}}:{{name}}={{score}} {{/items}}");
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
  auto constexpr tmpl = injamm::fixed_string("{{#values}}{{@key}}={{this}}{{/values}}");
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
