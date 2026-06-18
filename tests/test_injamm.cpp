#include "injamm.hpp"
#include <glaze/glaze.hpp>
#include <catch2/catch_test_macros.hpp>
#include <climits>
#include <map>
#include <optional>
#include <set>
#include <vector>

// ---- テスト用データ型 ----
// (existing types: BcUser, BcUsersData, BcBoolData, BcNested, BcOuter, BcAddress, BcFounder, BcCompany, BcIfData)

struct BcRootData {
  std::string app_name{"injamm"};
  struct Inner {
    std::string version{"1.0"};
  } info{.version = "1.0"};
};

template <>
struct glz::meta<BcRootData> {
  static constexpr auto value = glz::object("app_name", &BcRootData::app_name, "info", &BcRootData::info);
};

template <>
struct glz::meta<BcRootData::Inner> {
  static constexpr auto value = glz::object("version", &BcRootData::Inner::version);
};

struct BcMapData {
  std::string host{"localhost"};
  int port{8080};
};

template <>
struct glz::meta<BcMapData> {
  static constexpr auto value = glz::object("host", &BcMapData::host, "port", &BcMapData::port);
};

struct BcMapWrapper {
  BcMapData config;
};

template <>
struct glz::meta<BcMapWrapper> {
  static constexpr auto value = glz::object("config", &BcMapWrapper::config);
};

struct BcLlData {
  long long val{};
};

template <>
struct glz::meta<BcLlData> {
  static constexpr auto value = glz::object("val", &BcLlData::val);
};

// (Rest of existing types...)

/**
 * @brief セクションテスト用のユーザーデータ型
 * @details 名前と年齢を持つ単純なユーザー構造体。BcUsersData の配列要素として使用する。
 */
struct BcUser {
  std::string name; /**< ユーザー名 */
  int age{};        /**< 年齢 */
};

/**
 * @brief ユーザーリストを保持するセクションテスト用データ型
 * @details BcUser の配列を保持し、セクション描画のテストに使用する。
 */
struct BcUsersData {
  std::vector<BcUser> users; /**< ユーザーの配列 */
};

/** @brief Glaze メタ情報: BcUser の JSON シリアライズ定義 */
template <>
struct glz::meta<BcUser> {
  static constexpr auto value = glz::object("name", &BcUser::name, "age", &BcUser::age);
};

/** @brief Glaze メタ情報: BcUsersData の JSON シリアライズ定義 */
template <>
struct glz::meta<BcUsersData> {
  static constexpr auto value = glz::object("users", &BcUsersData::users);
};

/**
 * @brief 真偽値セクションテスト用データ型
 * @details 単一の bool フラグを持ち、セクションの真偽判定をテストする。
 */
struct BcBoolData {
  bool flag{}; /**< 真偽値フラグ */
};

/** @brief Glaze メタ情報: BcBoolData の JSON シリアライズ定義 */
template <>
struct glz::meta<BcBoolData> {
  static constexpr auto value = glz::object("flag", &BcBoolData::flag);
};

/**
 * @brief ネストセクションテスト用の内部データ型
 * @details BcOuter の配列要素として使用される。
 */
struct BcNested {
  std::string inner; /**< 内部文字列 */
};

/**
 * @brief ネストセクションテスト用の外部データ型
 * @details BcNested の配列を保持し、入れ子になったセクション描画をテストする。
 */
struct BcOuter {
  std::vector<BcNested> items; /**< ネストされた要素の配列 */
};

/** @brief Glaze メタ情報: BcNested の JSON シリアライズ定義 */
template <>
struct glz::meta<BcNested> {
  static constexpr auto value = glz::object("inner", &BcNested::inner);
};

/** @brief Glaze メタ情報: BcOuter の JSON シリアライズ定義 */
template <>
struct glz::meta<BcOuter> {
  static constexpr auto value = glz::object("items", &BcOuter::items);
};

/**
 * @brief 実数フィルタテスト用データ型
 * @details double 型のフィールドを持ち、実数フィルタのテストに使用する。
 */
struct BcFloatData {
  double value{}; /**< 実数値 */
};

/** @brief Glaze メタ情報: BcFloatData の JSON シリアライズ定義 */
template <>
struct glz::meta<BcFloatData> {
  static constexpr auto value = glz::object("value", &BcFloatData::value);
};

// ---- テストケース ----

/**
 * @brief リテラル文字列のみのテンプレート描画テスト
 * @details 変数やセクションを含まないベタ文字列がそのまま出力されることを確認する。
 */
TEST_CASE("bc_literal", "[injamm]") {
  auto bc = injamm::engine<BcBoolData>("hello world");
  auto r = bc.render(BcBoolData{true});
  REQUIRE(r.has_value());
  REQUIRE(*r == "hello world");
}

/**
 * @brief 文字列変数の描画テスト
 * @details {{name}} がデータの name フィールドで置換されることを確認する。
 */
TEST_CASE("bc_var_string", "[injamm]") {
  BcUser data{"alice", 30};
  auto bc = injamm::engine<BcUser>("{{name}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "alice");
}

/**
 * @brief 整数変数の描画テスト
 * @details {{age}} がデータの age フィールドの文字列表現で置換されることを確認する。
 */
TEST_CASE("bc_var_int", "[injamm]") {
  BcUser data{"alice", 30};
  auto bc = injamm::engine<BcUser>("{{age}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "30");
}

/**
 * @brief セクション描画の基本テスト
 * @details {{#users}}...{{/users}} で各要素に対して内容が繰り返し描画されることを確認する。
 */
TEST_CASE("bc_section", "[injamm]") {
  BcUsersData data;
  data.users.push_back(BcUser{"alice", 30});
  data.users.push_back(BcUser{"bob", 25});
  auto bc = injamm::engine<BcUsersData>("{{#users}}{{name}}-{{age}}/{{/users}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "alice-30/bob-25/");
}

/**
 * @brief 空セクションの描画テスト
 * @details 配列が空の場合、セクション内の内容は描画されず、
 *          セクション直後の "none" のみが出力されることを確認する。
 */
TEST_CASE("bc_section_empty", "[injamm]") {
  BcUsersData data;
  auto bc = injamm::engine<BcUsersData>("{{#users}}{{name}}{{/users}}none");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "none");
}

/**
 * @brief 真偽値セクション（true）の描画テスト
 * @details フラグが true の場合、セクション内容が描画されることを確認する。
 */
TEST_CASE("bc_section_bool_true", "[injamm]") {
  BcBoolData data{true};
  auto bc = injamm::engine<BcBoolData>("{{#flag}}yes{{/flag}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "yes");
}

/**
 * @brief 真偽値セクション（false）の描画テスト
 * @details フラグが false の場合、セクション内容が出力されないことを確認する。
 */
TEST_CASE("bc_section_bool_false", "[injamm]") {
  BcBoolData data{false};
  auto bc = injamm::engine<BcBoolData>("{{#flag}}yes{{/flag}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "");
}

/**
 * @brief 逆セクション（true）の描画テスト
 * @details {{^flag}} はフラグが true の場合に内容を出力しないことを確認する。
 */
TEST_CASE("bc_inverted_true", "[injamm]") {
  BcBoolData data{true};
  auto bc = injamm::engine<BcBoolData>("{{^flag}}no{{/flag}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "");
}

/**
 * @brief 逆セクション（false）の描画テスト
 * @details {{^flag}} はフラグが false の場合に内容を出力することを確認する。
 */
TEST_CASE("bc_inverted_false", "[injamm]") {
  BcBoolData data{false};
  auto bc = injamm::engine<BcBoolData>("{{^flag}}no{{/flag}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "no");
}

/**
 * @brief @index 特殊変数のテスト
 * @details セクション内で {{loop.index}} が 0 から始まる連番を出力することを確認する。
 */
TEST_CASE("bc_at_index", "[injamm]") {
  BcUsersData data;
  data.users.push_back(BcUser{"a", 1});
  data.users.push_back(BcUser{"b", 2});
  data.users.push_back(BcUser{"c", 3});
  auto bc = injamm::engine<BcUsersData>("{{#users}}{{loop.index}}{{/users}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "012");
}

/**
 * @brief @index1 特殊変数のテスト
 * @details セクション内で {{loop.index1}} が 1 から始まる連番を出力することを確認する。
 */
TEST_CASE("bc_at_index1", "[injamm]") {
  BcUsersData data;
  data.users.push_back(BcUser{"a", 1});
  data.users.push_back(BcUser{"b", 2});
  data.users.push_back(BcUser{"c", 3});
  auto bc = injamm::engine<BcUsersData>("{{#users}}{{loop.index1}}:{{name}} {{/users}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "1:a 2:b 3:c ");
}

/**
 * @brief @index1 のセクション外（@root コンテキスト外）テスト
 * @details ループ外で @index1 が出力されないことを確認する。
 */
TEST_CASE("bc_at_index1_outside_loop", "[injamm]") {
  BcUsersData data;
  data.users.push_back(BcUser{"a", 1});
  auto bc = injamm::engine<BcUsersData>("X{{loop.index1}}Y");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "XY");
}

/**
 * @brief @size 特殊変数のテスト
 * @details セクション内で {{loop.size}} がループ総要素数を出力することを確認する。
 */
TEST_CASE("bc_at_size", "[injamm]") {
  BcUsersData data;
  data.users.push_back(BcUser{"a", 1});
  data.users.push_back(BcUser{"b", 2});
  data.users.push_back(BcUser{"c", 3});
  auto bc = injamm::engine<BcUsersData>("{{#users}}{{loop.index}}/{{loop.size}} {{/users}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "0/3 1/3 2/3 ");
}

/**
 * @brief @size のセクション外テスト
 * @details ループ外で @size が出力されないことを確認する。
 */
TEST_CASE("bc_at_size_outside_loop", "[injamm]") {
  BcUsersData data;
  data.users.push_back(BcUser{"a", 1});
  auto bc = injamm::engine<BcUsersData>("X{{loop.size}}Y");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "XY");
}

/**
 * @brief @size の空ループテスト
 * @details 空のループで @size が "0" を出力することを確認する。
 */
TEST_CASE("bc_at_size_empty_loop", "[injamm]") {
  BcUsersData data;
  auto bc = injamm::engine<BcUsersData>("{{#users}}{{loop.size}}|{{/users}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "");
}

/**
 * @brief @first 特殊変数のテスト
 * @details セクション内で {{loop.is_first}} が先頭要素のみ "true"、
 *          それ以外は "false" を出力することを確認する。
 */
TEST_CASE("bc_at_first", "[injamm]") {
  BcUsersData data;
  data.users.push_back(BcUser{"a", 1});
  data.users.push_back(BcUser{"b", 2});
  auto bc = injamm::engine<BcUsersData>("{{#users}}{{loop.is_first}}{{/users}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "truefalse");
}

/**
 * @brief @last 特殊変数のテスト
 * @details セクション内で {{loop.is_last}} が末尾要素のみ "true"、
 *          それ以外は "false" を出力することを確認する。
 */
TEST_CASE("bc_at_last", "[injamm]") {
  BcUsersData data;
  data.users.push_back(BcUser{"a", 1});
  data.users.push_back(BcUser{"b", 2});
  auto bc = injamm::engine<BcUsersData>("{{#users}}{{loop.is_last}}{{/users}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "falsetrue");
}

/**
 * @brief {{#loop.is_first}} セクション構文のテスト
 * @details {{#loop.is_first}}...{{/loop.is_first}} で先頭要素のみ描画されることを確認する。
 */
TEST_CASE("bc_at_first_section", "[injamm]") {
  BcUsersData data;
  data.users.push_back(BcUser{"a", 1});
  data.users.push_back(BcUser{"b", 2});
  data.users.push_back(BcUser{"c", 3});
  auto bc = injamm::engine<BcUsersData>("{{#users}}{{#loop.is_first}}<{{name}}>{{/loop.is_first}}{{/users}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "<a>");
}

/**
 * @brief {{#loop.is_last}} セクション構文のテスト
 * @details {{#loop.is_last}}...{{/loop.is_last}} で末尾要素のみ描画されることを確認する。
 */
TEST_CASE("bc_at_last_section", "[injamm]") {
  BcUsersData data;
  data.users.push_back(BcUser{"a", 1});
  data.users.push_back(BcUser{"b", 2});
  auto bc = injamm::engine<BcUsersData>("{{#users}}{{#loop.is_last}}<{{name}}>{{/loop.is_last}}{{/users}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "<b>");
}

/**
 * @brief {{^loop.is_first}} 逆セクション構文のテスト
 * @details {{^loop.is_first}}...{{/loop.is_first}} で先頭要素以外を描画することを確認する。
 */
TEST_CASE("bc_at_first_inverted_section", "[injamm]") {
  BcUsersData data;
  data.users.push_back(BcUser{"a", 1});
  data.users.push_back(BcUser{"b", 2});
  data.users.push_back(BcUser{"c", 3});
  auto bc = injamm::engine<BcUsersData>("{{#users}}{{^loop.is_first}}<{{name}}>{{/loop.is_first}}{{/users}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "<b><c>");
}

/**
 * @brief {{^loop.is_last}} 逆セクション構文のテスト
 * @details {{^loop.is_last}}...{{/loop.is_last}} で末尾要素以外を描画することを確認する。
 */
TEST_CASE("bc_at_last_inverted_section", "[injamm]") {
  BcUsersData data;
  data.users.push_back(BcUser{"a", 1});
  data.users.push_back(BcUser{"b", 2});
  auto bc = injamm::engine<BcUsersData>("{{#users}}{{^loop.is_last}}<{{name}}>{{/loop.is_last}}{{/users}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "<a>");
}

/**
 * @brief ネストセクションの描画テスト
 * @details オブジェクトの配列内のフィールドにアクセスするネスト構造が正しく描画されることを確認する。
 */
TEST_CASE("bc_nested_section", "[injamm]") {
  BcOuter data;
  data.items.push_back(BcNested{"x"});
  data.items.push_back(BcNested{"y"});
  auto bc = injamm::engine<BcOuter>("{{#items}}{{inner}}{{/items}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "xy");
}

/**
 * @brief 生出力（{{{...}}}）のテスト
 * @details トリプルマスタッシュ {{{name}}} は HTML エスケープを行わず、
 *          生の文字列をそのまま出力することを確認する。
 */
TEST_CASE("bc_raw_output", "[injamm]") {
  BcUser data{"<b>alice</b>", 30};
  auto bc = injamm::engine<BcUser>("{{{name}}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "<b>alice</b>");
}

/**
 * @brief HTML エスケープ出力のテスト
 * @details ダブルマスタッシュ {{name}} は HTML 特殊文字をエスケープして出力することを確認する。
 */
TEST_CASE("bc_escaped_output", "[injamm]") {
  BcUser data{"<b>alice</b>", 30};
  auto bc = injamm::engine<BcUser>("{{name}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "&lt;b&gt;alice&lt;/b&gt;");
}

/**
 * @brief 複数変数の描画テスト
 * @details テンプレート内に複数の変数がある場合、それぞれ正しく置換されることを確認する。
 */
TEST_CASE("bc_multiple_vars", "[injamm]") {
  BcUser data{"alice", 30};
  auto bc = injamm::engine<BcUser>("{{name}}:{{age}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "alice:30");
}

/**
 * @brief 複合テンプレートの描画テスト
 * @details セクション内で if/else と @last を組み合わせた複雑なテンプレートが
 *          正しく描画されることを確認する。
 *          @note 期待される出力: "users: alice (30), bob (25)."
 */
TEST_CASE("bc_complex_template", "[injamm]") {
  BcUsersData data;
  data.users.push_back(BcUser{"alice", 30});
  data.users.push_back(BcUser{"bob", 25});
  auto bc = injamm::engine<BcUsersData>(
    "users: {{#users}}{{name}} ({{age}}){{#if loop.is_last}}.{{else}}, {{/if}}{{/users}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "users: alice (30), bob (25).");
}

// ---- ネストパステスト ----

/**
 * @brief 住所データ型（ネストパス用）
 * @details ネストされたドット区切りパスアクセスのテストに使用する。
 */
struct BcAddress {
  std::string city;    /**< 市区町村 */
  std::string country; /**< 国 */
};

/**
 * @brief 創業者データ型（ネストパス用）
 * @details 名前と住所（BcAddress）を持つ。BcCompany のネストフィールドとして使用する。
 */
struct BcFounder {
  std::string name;    /**< 創業者名 */
  BcAddress address;   /**< 住所 */
};

/**
 * @brief 会社データ型（ネストパス用）
 * @details 会社名と創業者情報を持ち、ドット区切りパスアクセスのトップレベルデータとして使用する。
 */
struct BcCompany {
  std::string name;      /**< 会社名 */
  BcFounder founder;     /**< 創業者情報 */
};

/** @brief Glaze メタ情報: BcAddress の JSON シリアライズ定義 */
template <>
struct glz::meta<BcAddress> {
  static constexpr auto value = glz::object("city", &BcAddress::city, "country", &BcAddress::country);
};

/** @brief Glaze メタ情報: BcFounder の JSON シリアライズ定義 */
template <>
struct glz::meta<BcFounder> {
  static constexpr auto value = glz::object("name", &BcFounder::name, "address", &BcFounder::address);
};

/** @brief Glaze メタ情報: BcCompany の JSON シリアライズ定義 */
template <>
struct glz::meta<BcCompany> {
  static constexpr auto value = glz::object("name", &BcCompany::name, "founder", &BcCompany::founder);
};

/**
 * @brief 単純なネストパスアクセスのテスト
 * @details {{founder.name}} がドット区切りパスを正しく解決し、創業者名を出力することを確認する。
 */
TEST_CASE("bc_nested_path_simple", "[injamm]") {
  auto bc = injamm::engine<BcCompany>("{{founder.name}}");
  BcCompany data{.name = "Acme", .founder = BcFounder{.name = "John", .address = {"NYC", "USA"}}};
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "John");
}

/**
 * @brief 深いネストパスアクセスのテスト
 * @details {{founder.address.city}} が 2 階層のドット区切りパスを正しく解決し、
 *          市区町村名を出力することを確認する。
 */
TEST_CASE("bc_nested_path_deep", "[injamm]") {
  auto bc = injamm::engine<BcCompany>("{{founder.address.city}}");
  BcCompany data{.name = "Acme", .founder = BcFounder{.name = "John", .address = {"NYC", "USA"}}};
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "NYC");
}

// ---- if/else テスト ----

/**
 * @brief if/else テスト用データ型
 * @details 名前と年齢を持ち、age フィールドの Truthy/Falsy 判定で if ブロックの制御を行う。
 */
struct BcIfData {
  std::string name; /**< 名前 */
  int age{};        /**< 年齢（0 は Falsy として扱う） */
};

/** @brief Glaze メタ情報: BcIfData の JSON シリアライズ定義 */
template <>
struct glz::meta<BcIfData> {
  static constexpr auto value = glz::object("name", &BcIfData::name, "age", &BcIfData::age);
};

/** @brief std::optional の Boolean 扱いをテストするためのデータ型 */
struct BcOptionalData {
  std::optional<std::string> opt_str;
};

/** @brief Glaze メタ情報: BcOptionalData */
template <>
struct glz::meta<BcOptionalData> {
  static constexpr auto value = glz::object("opt_str", &BcOptionalData::opt_str);
};

/**
 * @brief if 文（真）の描画テスト
 * @details {{#if age}} が非ゼロ値に対して内容を出力することを確認する。
 */
TEST_CASE("bc_if_bool_true", "[injamm]") {
  auto bc = injamm::engine<BcIfData>("{{#if age}}adult{{/if}}");
  BcIfData data{"alice", 20};
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "adult");
}

/**
 * @brief if 文（偽）の描画テスト
 * @details {{#if age}} がゼロ値に対して内容を出力しないことを確認する。
 */
TEST_CASE("bc_if_bool_false", "[injamm]") {
  auto bc = injamm::engine<BcIfData>("{{#if age}}adult{{/if}}");
  BcIfData data{"alice", 0};
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "");
}

/**
 * @brief if/else 文（真）の描画テスト
 * @details {{#if age}}...{{else}}...{{/if}} で真の場合に if 節が出力されることを確認する。
 */
TEST_CASE("bc_if_else_true", "[injamm]") {
  auto bc = injamm::engine<BcIfData>("{{#if age}}A{{else}}B{{/if}}");
  BcIfData data{"alice", 20};
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "A");
}

/**
 * @brief if/else 文（偽）の描画テスト
 * @details {{#if age}}...{{else}}...{{/if}} で偽の場合に else 節が出力されることを確認する。
 */
TEST_CASE("bc_if_else_false", "[injamm]") {
  auto bc = injamm::engine<BcIfData>("{{#if age}}A{{else}}B{{/if}}");
  BcIfData data{"alice", 0};
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "B");
}

/**
 * @brief if 文と @last の組み合わせテスト
 * @details セクション内で {{loop.is_last}} を if 条件に使用し、末尾要素のみドットを追記する動作を確認する。
 */
TEST_CASE("bc_if_with_at_last", "[injamm]") {
  auto bc = injamm::engine<BcUsersData>("{{#users}}{{name}}{{#if loop.is_last}}.{{/if}}{{/users}}");
  BcUsersData data{.users = {{"a", 1}, {"b", 2}}};
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "ab.");
}

/**
 * @brief if/else とセクションの組み合わせテスト
 * @details セクション内で @last による if/else 分岐を行い、
 *          カンマ区切りと末尾のピリオドが正しく出力されることを確認する。
 */
TEST_CASE("bc_if_else_with_section", "[injamm]") {
  auto bc = injamm::engine<BcUsersData>("{{#users}}{{name}}{{#if loop.is_last}}.{{else}},{{/if}}{{/users}}");
  BcUsersData data{.users = {{"a", 1}, {"b", 2}, {"c", 3}}};
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "a,b,c.");
}

TEST_CASE("bc_at_root", "[injamm]") {
  BcRootData data;
  auto bc = injamm::engine<BcRootData>("app: {{root}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "app: ");
}

TEST_CASE("bc_at_root_field_simple", "[injamm]") {
  BcRootData data;
  auto bc = injamm::engine<BcRootData>("{{root.app_name}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "injamm");
}

TEST_CASE("bc_at_root_field_nested", "[injamm]") {
  BcRootData data;
  auto bc = injamm::engine<BcRootData>("{{root.info.version}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "1.0");
}

TEST_CASE("bc_at_key_array", "[injamm]") {
  BcUsersData data;
  data.users.push_back(BcUser{"a", 1});
  data.users.push_back(BcUser{"b", 2});
  auto bc = injamm::engine<BcUsersData>("{{#users}}{{loop.key}}:{{name}},{{/users}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "0:a,1:b,");
}

TEST_CASE("bc_at_key_struct", "[injamm]") {
  BcMapWrapper data;
  auto bc = injamm::engine<BcMapWrapper>("{{#config}}{{loop.key}}={{this}};{{/config}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "host=localhost;port=8080;");
}

TEST_CASE("bc_struct_iteration_nested", "[injamm]") {
  BcMapWrapper data;
  auto bc = injamm::engine<BcMapWrapper>("{{#config}}{{#if loop.key}}k{{/if}}{{/config}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "kk");
}

// ---- フィルタテスト ----

TEST_CASE("filter: upper", "[filter]") {
  BcUser data{"hello world", 30};
  auto bc = injamm::engine<BcUser>("{{name | upper}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "HELLO WORLD");
}

TEST_CASE("filter: lower", "[filter]") {
  BcUser data{"HELLO WORLD", 30};
  auto bc = injamm::engine<BcUser>("{{name | lower}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "hello world");
}

TEST_CASE("filter: capitalize", "[filter]") {
  BcUser data{"hello world", 30};
  auto bc = injamm::engine<BcUser>("{{name | capitalize}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "Hello world");
}

TEST_CASE("filter: title", "[filter]") {
  BcUser data{"hello world", 30};
  auto bc = injamm::engine<BcUser>("{{name | title}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "Hello World");
}

TEST_CASE("filter: trim", "[filter]") {
  BcUser data{"  hello  ", 30};
  auto bc = injamm::engine<BcUser>("{{name | trim}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "hello");
}

TEST_CASE("filter: ltrim", "[filter]") {
  BcUser data{"  hello  ", 30};
  auto bc = injamm::engine<BcUser>("{{name | ltrim}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "hello  ");
}

TEST_CASE("filter: rtrim", "[filter]") {
  BcUser data{"  hello  ", 30};
  auto bc = injamm::engine<BcUser>("{{name | rtrim}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "  hello");
}

TEST_CASE("filter: chaining", "[filter]") {
  BcUser data{"  hello  ", 30};
  auto bc = injamm::engine<BcUser>("{{name | trim | upper}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "HELLO");
}

TEST_CASE("filter: raw output", "[filter]") {
  BcUser data{"  <script>  ", 30};
  auto bc = injamm::engine<BcUser>("{{{name | trim}}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "<script>");
}

// ---- 整数フィルタテスト ----

TEST_CASE("int_filter: abs positive", "[int_filter]") {
  BcIfData data{"test", 42};
  auto bc = injamm::engine<BcIfData>("{{age | abs}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "42");
}

TEST_CASE("int_filter: abs negative", "[int_filter]") {
  BcIfData data{"test", -42};
  auto bc = injamm::engine<BcIfData>("{{age | abs}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "42");
}

TEST_CASE("int_filter: hex", "[int_filter]") {
  BcIfData data{"test", 255};
  auto bc = injamm::engine<BcIfData>("{{age | hex}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "ff");
}

TEST_CASE("int_filter: oct", "[int_filter]") {
  BcIfData data{"test", 64};
  auto bc = injamm::engine<BcIfData>("{{age | oct}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "100");
}

TEST_CASE("int_filter: bin", "[int_filter]") {
  BcIfData data{"test", 10};
  auto bc = injamm::engine<BcIfData>("{{age | bin}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "1010");
}

TEST_CASE("int_filter: bin zero", "[int_filter]") {
  BcIfData data{"test", 0};
  auto bc = injamm::engine<BcIfData>("{{age | bin}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "0");
}

// ---- 引数付き文字列フィルタテスト ----

TEST_CASE("filter: left", "[filter]") {
  BcUser data{"hi", 30};
  auto bc = injamm::engine<BcUser>("{{name | left(5)}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "   hi");
}

TEST_CASE("filter: right", "[filter]") {
  BcUser data{"hi", 30};
  auto bc = injamm::engine<BcUser>("{{name | right(5)}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "hi   ");
}

TEST_CASE("filter: center", "[filter]") {
  BcUser data{"hi", 30};
  auto bc = injamm::engine<BcUser>("{{name | center(6)}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "  hi  ");
}

TEST_CASE("filter: truncate short", "[filter]") {
  BcUser data{"hello", 30};
  auto bc = injamm::engine<BcUser>("{{name | truncate(10)}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "hello");
}

TEST_CASE("filter: truncate long", "[filter]") {
  BcUser data{"hello world", 30};
  auto bc = injamm::engine<BcUser>("{{name | truncate(8)}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "hello...");
}

TEST_CASE("filter: substr one arg", "[filter]") {
  BcUser data{"hello", 30};
  auto bc = injamm::engine<BcUser>("{{name | substr(2)}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "llo");
}

TEST_CASE("filter: substr two args", "[filter]") {
  BcUser data{"hello", 30};
  auto bc = injamm::engine<BcUser>("{{name | substr(1,3)}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "ell");
}

// ---- 新しい整数フィルタテスト ----

TEST_CASE("int_filter: neg positive", "[int_filter]") {
  BcIfData data{"test", 42};
  auto bc = injamm::engine<BcIfData>("{{age | neg}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "-42");
}

TEST_CASE("int_filter: neg negative", "[int_filter]") {
  BcIfData data{"test", -10};
  auto bc = injamm::engine<BcIfData>("{{age | neg}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "10");
}

TEST_CASE("int_filter: mod", "[int_filter]") {
  BcIfData data{"test", 17};
  auto bc = injamm::engine<BcIfData>("{{age | mod(5)}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "2");
}

TEST_CASE("int_filter: mod by zero", "[int_filter]") {
  BcIfData data{"test", 17};
  auto bc = injamm::engine<BcIfData>("{{age | mod(0)}}");
  auto result = bc.render(data);
  REQUIRE(!result);
  REQUIRE(result.error().ec == injamm::error_code::division_by_zero);
}

TEST_CASE("int_filter: numify", "[int_filter]") {
  BcIfData data{"test", 1234567};
  auto bc = injamm::engine<BcIfData>("{{age | numify}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "1,234,567");
}

TEST_CASE("int_filter: numify small", "[int_filter]") {
  BcIfData data{"test", 123};
  auto bc = injamm::engine<BcIfData>("{{age | numify}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "123");
}

TEST_CASE("int_filter: numify negative", "[int_filter]") {
  BcIfData data{"test", -9876};
  auto bc = injamm::engine<BcIfData>("{{age | numify}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "-9,876");
}

TEST_CASE("int_filter: zerofill", "[int_filter]") {
  BcIfData data{"test", 42};
  auto bc = injamm::engine<BcIfData>("{{age | zerofill(5)}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "00042");
}

TEST_CASE("int_filter: zerofill exact width", "[int_filter]") {
  BcIfData data{"test", 123};
  auto bc = injamm::engine<BcIfData>("{{age | zerofill(3)}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "123");
}

TEST_CASE("int_filter: zerofill already wider", "[int_filter]") {
  BcIfData data{"test", 12345};
  auto bc = injamm::engine<BcIfData>("{{age | zerofill(3)}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "12345");
}

TEST_CASE("int_filter: zerofill negative", "[int_filter]") {
  BcIfData data{"test", -42};
  auto bc = injamm::engine<BcIfData>("{{age | zerofill(5)}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "-0042");
}

TEST_CASE("int_filter: zerofill zero", "[int_filter]") {
  BcIfData data{"test", 0};
  auto bc = injamm::engine<BcIfData>("{{age | zerofill(4)}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "0000");
}

// ---- LLONG_MIN UB 回避テスト ----

TEST_CASE("int_filter: abs LLONG_MIN", "[int_filter]") {
  BcLlData data{LLONG_MIN};
  auto bc = injamm::engine<BcLlData>("{{val | abs}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "9223372036854775808");
}

TEST_CASE("int_filter: neg LLONG_MIN", "[int_filter]") {
  BcLlData data{LLONG_MIN};
  auto bc = injamm::engine<BcLlData>("{{val | neg}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "9223372036854775808");
}

TEST_CASE("int_filter: numify LLONG_MIN", "[int_filter]") {
  BcLlData data{LLONG_MIN};
  auto bc = injamm::engine<BcLlData>("{{val | numify}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "-9,223,372,036,854,775,808");
}

TEST_CASE("int_filter: zerofill LLONG_MIN", "[int_filter]") {
  BcLlData data{LLONG_MIN};
  auto bc = injamm::engine<BcLlData>("{{val | zerofill(25)}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "-000009223372036854775808");
}

// ---- 比較フィルタ プレースホルダテスト ----

TEST_CASE("int_filter: ne true", "[int_filter]") {
  BcIfData data{"test", 42};
  auto bc = injamm::engine<BcIfData>("{{age | ne(10)}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "true");
}

TEST_CASE("int_filter: ne false", "[int_filter]") {
  BcIfData data{"test", 10};
  auto bc = injamm::engine<BcIfData>("{{age | ne(10)}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "false");
}

TEST_CASE("int_filter: gt true", "[int_filter]") {
  BcIfData data{"test", 20};
  auto bc = injamm::engine<BcIfData>("{{age | gt(18)}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "true");
}

TEST_CASE("int_filter: gt false", "[int_filter]") {
  BcIfData data{"test", 10};
  auto bc = injamm::engine<BcIfData>("{{age | gt(18)}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "false");
}

TEST_CASE("int_filter: gt equal_is_false", "[int_filter]") {
  BcIfData data{"test", 18};
  auto bc = injamm::engine<BcIfData>("{{age | gt(18)}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "false");
}

TEST_CASE("int_filter: gte true", "[int_filter]") {
  BcIfData data{"test", 18};
  auto bc = injamm::engine<BcIfData>("{{age | gte(18)}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "true");
}

TEST_CASE("int_filter: gte false", "[int_filter]") {
  BcIfData data{"test", 10};
  auto bc = injamm::engine<BcIfData>("{{age | gte(18)}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "false");
}

TEST_CASE("int_filter: lt true", "[int_filter]") {
  BcIfData data{"test", 5};
  auto bc = injamm::engine<BcIfData>("{{age | lt(10)}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "true");
}

TEST_CASE("int_filter: lt false", "[int_filter]") {
  BcIfData data{"test", 20};
  auto bc = injamm::engine<BcIfData>("{{age | lt(10)}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "false");
}

TEST_CASE("int_filter: lt equal_is_false", "[int_filter]") {
  BcIfData data{"test", 10};
  auto bc = injamm::engine<BcIfData>("{{age | lt(10)}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "false");
}

TEST_CASE("int_filter: lte true", "[int_filter]") {
  BcIfData data{"test", 10};
  auto bc = injamm::engine<BcIfData>("{{age | lte(10)}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "true");
}

TEST_CASE("int_filter: lte false", "[int_filter]") {
  BcIfData data{"test", 20};
  auto bc = injamm::engine<BcIfData>("{{age | lte(10)}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "false");
}

TEST_CASE("int_filter: ne true (float)", "[int_filter]") {
  BcFloatData data{2.5};
  auto bc = injamm::engine<BcFloatData>("{{value | ne(3)}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "true");
}

TEST_CASE("int_filter: gt float", "[int_filter]") {
  BcFloatData data{3.14};
  auto bc = injamm::engine<BcFloatData>("{{value | gt(3)}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "true");
}

TEST_CASE("int_filter: lt float false", "[int_filter]") {
  BcFloatData data{2.5};
  auto bc = injamm::engine<BcFloatData>("{{value | lt(2)}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "false");
}

TEST_CASE("int_filter: parse_failure_is_false", "[int_filter]") {
  BcIfData data{"test", 42};
  auto bc = injamm::engine<BcIfData>("{{name | gt(0)}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "false");
}

// ---- if フィルタテスト ----

TEST_CASE("bc_if_filter_is_neg_true", "[if_filter]") {
  auto bc = injamm::engine<BcIfData>("{{#if age | is_neg}}neg{{/if}}");
  BcIfData data{"test", -5};
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "neg");
}

TEST_CASE("bc_if_filter_is_neg_false", "[if_filter]") {
  auto bc = injamm::engine<BcIfData>("{{#if age | is_neg}}neg{{/if}}");
  BcIfData data{"test", 5};
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "");
}

TEST_CASE("bc_if_filter_is_neg_zero", "[if_filter]") {
  auto bc = injamm::engine<BcIfData>("{{#if age | is_neg}}neg{{/if}}");
  BcIfData data{"test", 0};
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "");
}

TEST_CASE("bc_if_filter_eq_true", "[if_filter]") {
  auto bc = injamm::engine<BcIfData>("{{#if age | eq(20)}}yes{{/if}}");
  BcIfData data{"test", 20};
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "yes");
}

TEST_CASE("bc_if_filter_eq_false", "[if_filter]") {
  auto bc = injamm::engine<BcIfData>("{{#if age | eq(20)}}yes{{/if}}");
  BcIfData data{"test", 10};
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "");
}

TEST_CASE("bc_if_filter_mod_eq", "[if_filter]") {
  auto bc = injamm::engine<BcIfData>("{{#if age | mod(4) | eq(2)}}match{{/if}}");
  BcIfData data{"test", 6};
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "match");
}

TEST_CASE("bc_if_filter_mod_eq_no", "[if_filter]") {
  auto bc = injamm::engine<BcIfData>("{{#if age | mod(4) | eq(2)}}match{{/if}}");
  BcIfData data{"test", 5};
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "");
}

TEST_CASE("bc_if_filter_ne_true", "[if_filter]") {
  auto bc = injamm::engine<BcIfData>("{{#if age | ne(20)}}diff{{/if}}");
  BcIfData data{"test", 10};
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "diff");
}

TEST_CASE("bc_if_filter_gt_true", "[if_filter]") {
  auto bc = injamm::engine<BcIfData>("{{#if age | gt(18)}}adult{{else}}minor{{/if}}");
  BcIfData data{"test", 25};
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "adult");
}

TEST_CASE("bc_if_filter_gt_false", "[if_filter]") {
  auto bc = injamm::engine<BcIfData>("{{#if age | gt(18)}}adult{{else}}minor{{/if}}");
  BcIfData data{"test", 15};
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "minor");
}

TEST_CASE("bc_if_filter_gte_true", "[if_filter]") {
  auto bc = injamm::engine<BcIfData>("{{#if age | gte(18)}}ok{{/if}}");
  BcIfData data{"test", 18};
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "ok");
}

TEST_CASE("bc_if_filter_lt_false", "[if_filter]") {
  auto bc = injamm::engine<BcIfData>("{{#if age | lt(0)}}neg{{/if}}");
  BcIfData data{"test", 5};
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "");
}

TEST_CASE("bc_if_filter_lte_true", "[if_filter]") {
  auto bc = injamm::engine<BcIfData>("{{#if age | lte(10)}}small{{/if}}");
  BcIfData data{"test", 10};
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "small");
}

TEST_CASE("bc_if_filter_chain_ne", "[if_filter]") {
  auto bc = injamm::engine<BcIfData>("{{#if age | mod(4) | ne(0)}}not_divisible{{/if}}");
  BcIfData data{"test", 5};
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "not_divisible");
}

TEST_CASE("bc_if_filter_with_else", "[if_filter]") {
  auto bc = injamm::engine<BcIfData>("{{#if age | is_neg}}neg{{else}}pos{{/if}}");
  BcIfData data{"test", -3};
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "neg");
}

TEST_CASE("bc_if_filter_else_false", "[if_filter]") {
  auto bc = injamm::engine<BcIfData>("{{#if age | is_neg}}neg{{else}}pos{{/if}}");
  BcIfData data{"test", 3};
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "pos");
}

TEST_CASE("bc_if_plain_still_works", "[if_filter]") {
  auto bc = injamm::engine<BcIfData>("{{#if age}}adult{{/if}}");
  BcIfData data{"test", 20};
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "adult");
}

// ---- ネストしたセクション内 else のテスト ----

TEST_CASE("if with nested section: else belongs to if", "[if][section]") {
  BcUsersData data{{{"Alice", 30}, {"Bob", 25}}};
  // {{#if users}} の else は、users が空のとき描画される
  // {{#users}} 内の {{else}} は無視される
  auto bc = injamm::engine<BcUsersData>(
    "{{#if users}}"
    "{{#users}}{{name}} {{/users}}"
    "{{else}}"
    "no users"
    "{{/if}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "Alice Bob ");
}

TEST_CASE("if with nested section: else triggers on empty", "[if][section]") {
  BcUsersData data{{}};
  auto bc = injamm::engine<BcUsersData>(
    "{{#if users}}"
    "{{#users}}{{name}} {{/users}}"
    "{{else}}"
    "no users"
    "{{/if}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "no users");
}

TEST_CASE("deeply nested: if inside section", "[if][section]") {
  BcUsersData data{{{"Alice", 30}, {"Bob", 25}}};
  auto bc = injamm::engine<BcUsersData>(
    "{{#users}}"
    "{{#if loop.is_first}}"
    "First: {{name}}"
    "{{else}}"
    "Other: {{name}}"
    "{{/if}}"
    "{{/users}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "First: AliceOther: Bob");
}

// ---- 実数フィルタテスト ----

TEST_CASE("float_filter: abs positive", "[float_filter]") {
  BcFloatData data{3.14};
  auto bc = injamm::engine<BcFloatData>("{{value | abs}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "3.14");
}

TEST_CASE("float_filter: abs negative", "[float_filter]") {
  BcFloatData data{-3.14};
  auto bc = injamm::engine<BcFloatData>("{{value | abs}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "3.14");
}

TEST_CASE("float_filter: neg positive", "[float_filter]") {
  BcFloatData data{3.14};
  auto bc = injamm::engine<BcFloatData>("{{value | neg}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "-3.14");
}

TEST_CASE("float_filter: neg negative", "[float_filter]") {
  BcFloatData data{-3.14};
  auto bc = injamm::engine<BcFloatData>("{{value | neg}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "3.14");
}

TEST_CASE("float_filter: precision", "[float_filter]") {
  BcFloatData data{3.14159265};
  auto bc = injamm::engine<BcFloatData>("{{value | precision(2)}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "3.14");
}

TEST_CASE("float_filter: precision zero", "[float_filter]") {
  BcFloatData data{3.14159265};
  auto bc = injamm::engine<BcFloatData>("{{value | precision(0)}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "3");
}

TEST_CASE("float_filter: precision four", "[float_filter]") {
  BcFloatData data{3.14159265};
  auto bc = injamm::engine<BcFloatData>("{{value | precision(4)}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "3.1416");
}

TEST_CASE("float_filter: numify integer", "[float_filter]") {
  BcFloatData data{1234567.0};
  auto bc = injamm::engine<BcFloatData>("{{value | numify}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "1,234,567");
}

TEST_CASE("float_filter: numify fractional", "[float_filter]") {
  BcFloatData data{1234567.89};
  auto bc = injamm::engine<BcFloatData>("{{value | numify}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "1,234,567.89");
}

TEST_CASE("float_filter: numify negative", "[float_filter]") {
  BcFloatData data{-9876.54};
  auto bc = injamm::engine<BcFloatData>("{{value | numify}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "-9,876.54");
}

TEST_CASE("float_filter: numify small", "[float_filter]") {
  BcFloatData data{123.45};
  auto bc = injamm::engine<BcFloatData>("{{value | numify}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "123.45");
}

TEST_CASE("float_filter: chaining", "[float_filter]") {
  BcFloatData data{-1234.5678};
  auto bc = injamm::engine<BcFloatData>("{{value | abs | precision(2)}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "1234.57");
}

TEST_CASE("float_filter: abs zero", "[float_filter]") {
  BcFloatData data{0.0};
  auto bc = injamm::engine<BcFloatData>("{{value | abs}}");
  auto result = bc.render(data);
  REQUIRE(result);
  REQUIRE(*result == "0");
}

// ---- std::optional の Boolean 扱い ----

TEST_CASE("bc_optional_section_present", "[optional]") {
  BcOptionalData data{std::optional<std::string>{"hello"}};
  auto bc = injamm::engine<BcOptionalData>("{{#opt_str}}yes{{/opt_str}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "yes");
}

TEST_CASE("bc_optional_section_empty", "[optional]") {
  BcOptionalData data{std::nullopt};
  auto bc = injamm::engine<BcOptionalData>("{{#opt_str}}yes{{/opt_str}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "");
}

TEST_CASE("bc_optional_if_present", "[optional]") {
  BcOptionalData data{std::optional<std::string>{"hello"}};
  auto bc = injamm::engine<BcOptionalData>("{{#if opt_str}}yes{{/if}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "yes");
}

TEST_CASE("bc_optional_if_empty", "[optional]") {
  BcOptionalData data{std::nullopt};
  auto bc = injamm::engine<BcOptionalData>("{{#if opt_str}}yes{{/if}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "");
}

TEST_CASE("bc_optional_if_else_present", "[optional]") {
  BcOptionalData data{std::optional<std::string>{"hello"}};
  auto bc = injamm::engine<BcOptionalData>("{{#if opt_str}}yes{{else}}no{{/if}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "yes");
}

TEST_CASE("bc_optional_if_else_empty", "[optional]") {
  BcOptionalData data{std::nullopt};
  auto bc = injamm::engine<BcOptionalData>("{{#if opt_str}}yes{{else}}no{{/if}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "no");
}

TEST_CASE("bc_optional_inverted_present", "[optional]") {
  BcOptionalData data{std::optional<std::string>{"hello"}};
  auto bc = injamm::engine<BcOptionalData>("{{^opt_str}}no{{/opt_str}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "");
}

TEST_CASE("bc_optional_inverted_empty", "[optional]") {
  BcOptionalData data{std::nullopt};
  auto bc = injamm::engine<BcOptionalData>("{{^opt_str}}no{{/opt_str}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "no");
}

TEST_CASE("bc_optional_var_present", "[optional]") {
  BcOptionalData data{std::optional<std::string>{"hello"}};
  auto bc = injamm::engine<BcOptionalData>("{{opt_str}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "hello");
}

TEST_CASE("bc_optional_var_empty", "[optional]") {
  BcOptionalData data{std::nullopt};
  auto bc = injamm::engine<BcOptionalData>("{{opt_str}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "");
}

// ---- break/continue テスト ----

TEST_CASE("bc_break_basic", "[break]") {
  BcUsersData data{.users = {{"Alice", 30}, {"Bob", 25}, {"Charlie", 35}, {"Dave", 40}}};
  auto bc = injamm::engine<BcUsersData>("{{#users}}{{name}}:{{#break}}|{{/users}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "Alice:");
}

TEST_CASE("bc_continue_basic", "[continue]") {
  BcUsersData data{.users = {{"Alice", 30}, {"Bob", 25}, {"Charlie", 35}, {"Dave", 40}}};
  auto bc = injamm::engine<BcUsersData>("{{#users}}{{#continue}}{{name}}|{{/users}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "");
}

TEST_CASE("bc_continue_skip_second", "[continue]") {
  BcUsersData data{.users = {{"Alice", 30}, {"Bob", 25}, {"Charlie", 35}}};
  auto bc = injamm::engine<BcUsersData>("{{#users}}{{#if loop.is_first}}skip{{/if}}{{#if loop.is_last}}last{{/if}}{{name}}|{{/users}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "skipAlice|Bob|lastCharlie|");
}

TEST_CASE("bc_break_with_if", "[break]") {
  BcUsersData data{.users = {{"Alice", 30}, {"Bob", 25}, {"Charlie", 35}, {"Dave", 40}}};
  auto bc = injamm::engine<BcUsersData>("{{#users}}{{#if loop.is_last}}{{#break}}{{/if}}{{name}}|{{/users}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "Alice|Bob|Charlie|");
}

TEST_CASE("bc_continue_with_if", "[continue]") {
  BcUsersData data{.users = {{"Alice", 30}, {"Bob", 25}, {"Charlie", 35}, {"Dave", 40}}};
  auto bc = injamm::engine<BcUsersData>("{{#users}}{{#if loop.is_first}}{{#continue}}{{/if}}{{name}}|{{/users}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "Bob|Charlie|Dave|");
}

TEST_CASE("bc_break_nested_section", "[break]") {
  BcUsersData data{.users = {{"Alice", 30}, {"Bob", 25}, {"Charlie", 35}}};
  auto bc = injamm::engine<BcUsersData>("{{#users}}[{{#users}}{{#break}}{{name}}|{{/users}}]{{/users}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "[][][]");
}

TEST_CASE("bc_continue_nested_section", "[continue]") {
  BcUsersData data{.users = {{"Alice", 30}, {"Bob", 25}, {"Charlie", 35}}};
  auto bc = injamm::engine<BcUsersData>("{{#users}}[{{#users}}{{#continue}}x{{name}}|{{/users}}]{{/users}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "[][][]");
}

TEST_CASE("disassemble_simple_var", "[disassemble]") {
  auto bc = injamm::engine<BcUser>("Hello {{name}}!");
  auto asm_str = bc.disassemble();
  REQUIRE(asm_str.contains("--- instructions ---"));
  REQUIRE(asm_str.contains("emit_litvar"));
  REQUIRE(asm_str.contains("name"));
  REQUIRE(asm_str.contains("--- literals ---"));
  REQUIRE(asm_str.contains("--- var_refs ---"));
}

TEST_CASE("disassemble_raw_var", "[disassemble]") {
  auto bc = injamm::engine<BcUser>("{{{name}}}");
  auto asm_str = bc.disassemble();
  REQUIRE(asm_str.contains("emit_var_raw"));
  REQUIRE(asm_str.contains("name"));
}

TEST_CASE("disassemble_section", "[disassemble]") {
  auto bc = injamm::engine<BcUsersData>("{{#users}}{{name}}{{/users}}");
  auto asm_str = bc.disassemble();
  REQUIRE(asm_str.contains("emit_section"));
  REQUIRE(asm_str.contains("users"));
  REQUIRE(asm_str.contains("emit_end"));
}

TEST_CASE("disassemble_if", "[disassemble]") {
  auto bc = injamm::engine<BcUser>("{{#if name}}yes{{else}}no{{/if}}");
  auto asm_str = bc.disassemble();
  REQUIRE(asm_str.contains("emit_if"));
  REQUIRE(asm_str.contains("emit_else"));
  REQUIRE(asm_str.contains("emit_endif"));
}

TEST_CASE("disassemble_litvar_fusion", "[disassemble]") {
  auto bc = injamm::engine<BcUser>("Hello {{name}}");
  auto asm_str = bc.disassemble();
  REQUIRE(asm_str.contains("emit_litvar"));
  REQUIRE(asm_str.contains("Hello "));
  REQUIRE(asm_str.contains("name"));
}

TEST_CASE("disassemble_filter", "[disassemble]") {
  auto bc = injamm::engine<BcUser>("{{name | upper}}");
  auto asm_str = bc.disassemble();
  REQUIRE(asm_str.contains("resolve_filtered"));
  REQUIRE(asm_str.contains("emit_filtered"));
  REQUIRE(asm_str.contains("filters=[upper]"));
}

TEST_CASE("disassemble_inverted", "[disassemble]") {
  auto bc = injamm::engine<BcUsersData>("{{^users}}empty{{/users}}");
  auto asm_str = bc.disassemble();
  REQUIRE(asm_str.contains("emit_inverted"));
  REQUIRE(asm_str.contains("users"));
}

TEST_CASE("disassemble_field_index", "[disassemble]") {
  auto bc = injamm::engine<BcUser>("{{name}}");
  auto asm_str = bc.disassemble();
  REQUIRE(asm_str.contains("field="));
}

TEST_CASE("disassemble_empty_template", "[disassemble]") {
  auto bc = injamm::engine<BcUser>("plain text only");
  auto asm_str = bc.disassemble();
  REQUIRE(asm_str.contains("--- instructions ---"));
  REQUIRE(asm_str.contains("halt"));
  REQUIRE(asm_str.contains("\"plain text only\""));
}

// ---- std::map テスト用データ型 ----

struct BcMapIntData {
  std::map<std::string, int> values;
};

template <>
struct glz::meta<BcMapIntData> {
  static constexpr auto value = glz::object("values", &BcMapIntData::values);
};

struct BcMapStrData {
  std::map<std::string, std::string> labels;
};

template <>
struct glz::meta<BcMapStrData> {
  static constexpr auto value = glz::object("labels", &BcMapStrData::labels);
};

struct BcMapItem {
  std::string name;
  int score{};
};

template <>
struct glz::meta<BcMapItem> {
  static constexpr auto value = glz::object("name", &BcMapItem::name, "score", &BcMapItem::score);
};

struct BcMapStructData {
  std::map<std::string, BcMapItem> items;
};

template <>
struct glz::meta<BcMapStructData> {
  static constexpr auto value = glz::object("items", &BcMapStructData::items);
};

struct BcMapUmapData {
  std::unordered_map<std::string, int> counts;
};

template <>
struct glz::meta<BcMapUmapData> {
  static constexpr auto value = glz::object("counts", &BcMapUmapData::counts);
};

struct BcMapMixedData {
  std::string prefix;
  std::map<std::string, int> values;
};

template <>
struct glz::meta<BcMapMixedData> {
  static constexpr auto value = glz::object("prefix", &BcMapMixedData::prefix, "values", &BcMapMixedData::values);
};

// ---- std::map セクション反復テスト ----

TEST_CASE("bc_map_section_basic", "[injamm][bc][map]") {
  auto bc = injamm::engine<BcMapIntData>("{{#values}}{{loop.key}}={{this}} {{/values}}");
  BcMapIntData data{{ {"a", 1}, {"b", 2}, {"c", 3} }};
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "a=1 b=2 c=3 ");
}

TEST_CASE("bc_map_section_string_values", "[injamm][bc][map]") {
  auto bc = injamm::engine<BcMapStrData>("{{#labels}}{{loop.key}}:{{this}} {{/labels}}");
  BcMapStrData data{{ {"color", "red"}, {"size", "large"} }};
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "color:red size:large ");
}

TEST_CASE("bc_map_section_struct_values", "[injamm][bc][map]") {
  auto bc = injamm::engine<BcMapStructData>("{{#items}}{{loop.key}}:{{name}}={{score}} {{/items}}");
  BcMapStructData data{{ {"alice", {.name = "Alice", .score = 100}}, {"bob", {.name = "Bob", .score = 85}} }};
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "alice:Alice=100 bob:Bob=85 ");
}

TEST_CASE("bc_map_section_empty", "[injamm][bc][map]") {
  auto bc = injamm::engine<BcMapIntData>("before{{#values}}NEVER{{/values}}after");
  BcMapIntData data;
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "beforeafter");
}

TEST_CASE("bc_map_inverted_empty", "[injamm][bc][map]") {
  auto bc = injamm::engine<BcMapIntData>("{{^values}}empty{{/values}}");
  BcMapIntData data;
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "empty");
}

TEST_CASE("bc_map_inverted_nonempty", "[injamm][bc][map]") {
  auto bc = injamm::engine<BcMapIntData>("{{^values}}empty{{/values}}");
  BcMapIntData data{{ {"x", 1} }};
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "");
}

TEST_CASE("bc_map_if_true", "[injamm][bc][map]") {
  auto bc = injamm::engine<BcMapIntData>("{{#values}}has values{{/values}}");
  BcMapIntData data{{ {"x", 1} }};
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "has values");
}

TEST_CASE("bc_map_if_false", "[injamm][bc][map]") {
  auto bc = injamm::engine<BcMapIntData>("{{#values}}NEVER{{/values}}");
  BcMapIntData data;
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "");
}

TEST_CASE("bc_map_unordered", "[injamm][bc][map]") {
  auto bc = injamm::engine<BcMapUmapData>("{{#counts}}{{loop.key}}={{this}} {{/counts}}");
  BcMapUmapData data{{ {"x", 10}, {"y", 20} }};
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  // unordered_map の順序は保証されないが、キーと値のペアは含まれる
  REQUIRE(r->contains("x=10"));
  REQUIRE(r->contains("y=20"));
}

TEST_CASE("bc_map_with_prefix", "[injamm][bc][map]") {
  auto bc = injamm::engine<BcMapMixedData>("{{prefix}}: {{#values}}{{loop.key}}={{this}} {{/values}}");
  BcMapMixedData data{.prefix = "data", .values = {{"k", 42}}};
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "data: k=42 ");
}

TEST_CASE("bc_map_disassemble", "[injamm][bc][map]") {
  auto bc = injamm::engine<BcMapIntData>("{{#values}}{{loop.key}}={{this}}{{/values}}");
  auto asm_str = bc.disassemble();
  REQUIRE(asm_str.contains("emit_section"));
  REQUIRE(asm_str.contains("emit_at_key"));
  REQUIRE(asm_str.contains("emit_this"));
  REQUIRE(asm_str.contains("emit_end"));
}

TEST_CASE("bc_map_single_entry", "[injamm][bc][map]") {
  auto bc = injamm::engine<BcMapIntData>("{{#values}}{{loop.key}}={{this}}{{/values}}");
  BcMapIntData data{{ {"only", 99} }};
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "only=99");
}

TEST_CASE("bc_map_many_entries", "[injamm][bc][map]") {
  auto bc = injamm::engine<BcMapIntData>("{{#values}}{{loop.key}}={{this}} {{/values}}");
  BcMapIntData data;
  for (int i = 0; i < 10; ++i) {
    data.values[std::string(1, 'a' + i)] = i;
  }
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  for (int i = 0; i < 10; ++i) {
    std::string key(1, 'a' + i);
    REQUIRE(r->contains(key + "=" + std::to_string(i)));
  }
}

// ---- std::set テスト (bytecode VM) ----

struct BcSetIntData {
  std::set<int> values;
};

template <>
struct glz::meta<BcSetIntData> {
  static constexpr auto value = glz::object("values", &BcSetIntData::values);
};

struct AtVarUser {
  std::string name;
  int age{};
};

template <>
struct glz::meta<AtVarUser> {
  static constexpr auto value = glz::object("name", &AtVarUser::name, "age", &AtVarUser::age);
};

struct AtVarItem {
  std::string val;
};

template <>
struct glz::meta<AtVarItem> {
  static constexpr auto value = glz::object("val", &AtVarItem::val);
};

struct AtVarItemsCtx {
  std::vector<AtVarItem> items;
};

template <>
struct glz::meta<AtVarItemsCtx> {
  static constexpr auto value = glz::object("items", &AtVarItemsCtx::items);
};

TEST_CASE("bc_set_section", "[injamm][bc][set]") {
  auto bc = injamm::engine<BcSetIntData>("{{#values}}[{{this}}]{{/values}}");
  BcSetIntData data{{ {3, 1, 2} }};
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "[1][2][3]");
}

TEST_CASE("bc_set_empty", "[injamm][bc][set]") {
  auto bc = injamm::engine<BcSetIntData>("{{#values}}[{{this}}]{{/values}}");
  BcSetIntData data{};
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "");
}

TEST_CASE("bc_set_inverted_empty", "[injamm][bc][set]") {
  auto bc = injamm::engine<BcSetIntData>("{{^values}}empty{{/values}}");
  BcSetIntData data{};
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "empty");
}

TEST_CASE("bc_set_inverted_nonempty", "[injamm][bc][set]") {
  auto bc = injamm::engine<BcSetIntData>("{{^values}}empty{{/values}}");
  BcSetIntData data{{ {1} }};
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "");
}

TEST_CASE("bc_set_if", "[injamm][bc][set]") {
  auto bc = injamm::engine<BcSetIntData>("{{#values}}[{{this}}]{{/values}}");
  BcSetIntData data{{ {1, 2} }};
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "[1][2]");
}

TEST_CASE("bc_set_if_empty", "[injamm][bc][set]") {
  auto bc = injamm::engine<BcSetIntData>("{{#values}}[{{this}}]{{/values}}");
  BcSetIntData data{};
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "");
}

TEST_CASE("@var basic expansion in engine", "[injamm][atvar]") {
  AtVarUser ctx{"Alice", 30};
  std::map<std::string, std::string, std::less<>> consts{{"f", "name"}};
  injamm::engine<AtVarUser> eng{"Hello {{@var(f)}}!", consts};
  auto result = eng.render(ctx);
  REQUIRE(result.has_value());
  CHECK(*result == "Hello Alice!");
}

TEST_CASE("@var in section key engine", "[injamm][atvar]") {
  AtVarItemsCtx ctx{{{"A"}, {"B"}}};
  std::map<std::string, std::string, std::less<>> consts{{"s", "items"}};
  injamm::engine<AtVarItemsCtx> eng{"{{#@var(s)}}{{val}}{{/@var(s)}}", consts};
  auto result = eng.render(ctx);
  REQUIRE(result.has_value());
  CHECK(*result == "AB");
}

TEST_CASE("@var with filter in engine", "[injamm][atvar]") {
  AtVarUser ctx{"alice", 30};
  std::map<std::string, std::string, std::less<>> consts{{"f", "name"}};
  injamm::engine<AtVarUser> eng{"{{@var(f) | upper}}", consts};
  auto result = eng.render(ctx);
  REQUIRE(result.has_value());
  CHECK(*result == "ALICE");
}

TEST_CASE("@var undefined constant error in engine", "[injamm][atvar]") {
  AtVarUser ctx{"Alice", 30};
  std::map<std::string, std::string, std::less<>> consts{};
  injamm::engine<AtVarUser> eng{"{{@var(unknown)}}", consts};
  auto result = eng.render(ctx);
  REQUIRE(!result.has_value());
  CHECK(result.error().ec == injamm::error_code::unknown_key);
}

TEST_CASE("@var inside {{{}}} raw tag in engine", "[injamm][atvar]") {
  AtVarUser ctx{"Alice", 30};
  std::map<std::string, std::string, std::less<>> consts{{"f", "name"}};
  injamm::engine<AtVarUser> eng{"{{{@var(f)}}}", consts};
  auto result = eng.render(ctx);
  REQUIRE(result.has_value());
  CHECK(*result == "Alice");
}

TEST_CASE("error: unknown_filter on variable", "[error]") {
  auto bc = injamm::engine<BcUser>("{{name | bogus_filter_xyz}}");
  BcUser data{"test", 25};
  auto result = bc.render(data);
  REQUIRE(!result.has_value());
  CHECK(result.error().ec == injamm::error_code::unknown_filter);
}

TEST_CASE("error: unknown_filter on if", "[error]") {
  auto bc = injamm::engine<BcIfData>("{{#if age | bogus_xyz}}yes{{/if}}");
  BcIfData data{"test", 25};
  auto result = bc.render(data);
  REQUIRE(!result.has_value());
  CHECK(result.error().ec == injamm::error_code::unknown_filter);
}

TEST_CASE("error: @var circular reference detected", "[error][atvar]") {
  AtVarUser ctx{"Alice", 30};
  std::map<std::string, std::string, std::less<>> consts{
    {"a", "@var(b)"},
    {"b", "@var(a)"}
  };
  injamm::engine<AtVarUser> eng{"{{@var(a)}}", consts};
  auto result = eng.render(ctx);
  REQUIRE(!result.has_value());
  CHECK(result.error().ec == injamm::error_code::syntax_error);
}

// ---- loop.X 旧 @var 構文の互換性破棄確認 ----

TEST_CASE("legacy @index is rejected", "[injamm][loop][legacy]") {
  BcUsersData data;
  data.users.push_back(BcUser{"a", 1});
  auto eng = injamm::engine<BcUsersData>("{{#users}}{{@index}}{{/users}}");
  auto r = eng.render(data);
  // loop.index に置換されず、空文字として描画されるか、エラーになる。
  // 現状: ループ外フィールド扱いとして空出力（r.has_value() == true, 値 == ""）
  REQUIRE(r.has_value());
  CHECK(*r == "");
}

TEST_CASE("legacy @first is rejected", "[injamm][loop][legacy]") {
  BcUsersData data;
  data.users.push_back(BcUser{"a", 1});
  auto eng = injamm::engine<BcUsersData>("{{#users}}{{@first}}{{/users}}");
  auto r = eng.render(data);
  REQUIRE(r.has_value());
  CHECK(*r == "");
}

TEST_CASE("legacy @last is rejected", "[injamm][loop][legacy]") {
  BcUsersData data;
  data.users.push_back(BcUser{"a", 1});
  auto eng = injamm::engine<BcUsersData>("{{#users}}{{@last}}{{/users}}");
  auto r = eng.render(data);
  REQUIRE(r.has_value());
  CHECK(*r == "");
}

TEST_CASE("legacy @root.X is rejected (engine path)", "[injamm][loop][legacy]") {
  BcRootData data;
  auto eng = injamm::engine<BcRootData>("{{@root.app_name}}");
  auto r = eng.render(data);
  // @root.app_name は root プレフィックスが消えたただの "app_name" として扱われる
  // BcRootData のフィールドではないため空出力
  REQUIRE(r.has_value());
  CHECK(*r == "");
}

// ---- trim_blocks / lstrip_blocks tests ----

TEST_CASE("trim_blocks removes newline after }}", "[injamm][whitespace]") {
  BcUser data{"Alice", 30};
  auto eng = injamm::engine<BcUser>("a{{name}}\nb", true);
  auto r = eng.render(data);
  REQUIRE(r.has_value());
  CHECK(*r == "aAliceb");
}

TEST_CASE("trim_blocks does nothing when no newline follows", "[injamm][whitespace]") {
  BcUser data{"Alice", 30};
  auto eng = injamm::engine<BcUser>("{{name}}{{age}}", true);
  auto r = eng.render(data);
  REQUIRE(r.has_value());
  CHECK(*r == "Alice30");
}

TEST_CASE("trim_blocks with section open/close", "[injamm][whitespace]") {
  BcBoolData data{true};
  auto eng = injamm::engine<BcBoolData>("x{{#flag}}\ny\n{{/flag}}z", true);
  auto r = eng.render(data);
  REQUIRE(r.has_value());
  CHECK(*r == "xy\nz");
  // After {{#flag}}: \n removed. Body: "y\n". After {{/flag}}: no \n (next char is z).
}

TEST_CASE("trim_blocks with if/else", "[injamm][whitespace]") {
  SECTION("truthy age renders then body") {
    BcIfData data{"test", 25};
    auto eng = injamm::engine<BcIfData>("{{#if age}}\ny\n{{else}}\nn\n{{/if}}", true);
    auto r = eng.render(data);
    REQUIRE(r.has_value());
    CHECK(*r == "y\n");
  }
  SECTION("falsy age renders else body") {
    BcIfData data{"test", 0};
    auto eng = injamm::engine<BcIfData>("{{#if age}}\ny\n{{else}}\nn\n{{/if}}", true);
    auto r = eng.render(data);
    REQUIRE(r.has_value());
    CHECK(*r == "n\n");
  }
}

TEST_CASE("lstrip_blocks strips whitespace before section open", "[injamm][whitespace]") {
  BcBoolData data{true};
  auto eng = injamm::engine<BcBoolData>("a\n  {{#flag}}y{{/flag}}", false, true);
  auto r = eng.render(data);
  REQUIRE(r.has_value());
  CHECK(*r == "a\ny");
}

TEST_CASE("lstrip_blocks strips whitespace before section close", "[injamm][whitespace]") {
  BcBoolData data{true};
  auto eng = injamm::engine<BcBoolData>("{{#flag}}y\n  {{/flag}}", false, true);
  auto r = eng.render(data);
  REQUIRE(r.has_value());
  CHECK(*r == "y\n");
}

TEST_CASE("lstrip_blocks does not strip whitespace before expression tags", "[injamm][whitespace]") {
  BcUser data{"Alice", 30};
  auto eng = injamm::engine<BcUser>("  {{name}}  {{age}}", false, true);
  auto r = eng.render(data);
  REQUIRE(r.has_value());
  CHECK(*r == "  Alice  30");
}

TEST_CASE("lstrip_blocks with if/else", "[injamm][whitespace]") {
  SECTION("truthy age renders then body") {
    BcIfData data{"test", 25};
    auto eng = injamm::engine<BcIfData>("a\n  {{#if age}}\nyes\n  {{else}}\nno\n  {{/if}}", false, true);
    auto r = eng.render(data);
    REQUIRE(r.has_value());
    CHECK(*r == "a\n\nyes\n");
  }
  SECTION("falsy age renders else body") {
    BcIfData data{"test", 0};
    auto eng = injamm::engine<BcIfData>("a\n  {{#if age}}\nyes\n  {{else}}\nno\n  {{/if}}", false, true);
    auto r = eng.render(data);
    REQUIRE(r.has_value());
    CHECK(*r == "a\n\nno\n");
  }
}

TEST_CASE("trim_blocks + lstrip_blocks combined", "[injamm][whitespace]") {
  BcBoolData data{true};
  auto eng = injamm::engine<BcBoolData>("{{#flag}}\n  y\n{{/flag}}", true, true);
  auto r = eng.render(data);
  REQUIRE(r.has_value());
  CHECK(*r == "  y\n");
}

TEST_CASE("trim_blocks with nested sections", "[injamm][whitespace]") {
  BcOuter data{{{"a"}, {"b"}}};
  auto eng = injamm::engine<BcOuter>("{{#items}}\n{{inner}}\n{{/items}}", true);
  auto r = eng.render(data);
  REQUIRE(r.has_value());
  CHECK(*r == "ab");
}

TEST_CASE("comment_basic", "[injamm][comment]") {
  BcUser data{"Alice", 30};
  auto bc = injamm::engine<BcUser>("Hello {# this is a comment #}{{name}}!");
  auto result = bc.render(data);
  REQUIRE(result.has_value());
  CHECK(*result == "Hello Alice!");
}

TEST_CASE("bang_comment_basic", "[injamm][comment]") {
  BcUser data{"Alice", 30};
  auto bc = injamm::engine<BcUser>("Hello {{! this is a comment }}{{name}}!");
  auto result = bc.render(data);
  REQUIRE(result.has_value());
  CHECK(*result == "Hello Alice!");
}

TEST_CASE("bang_comment_with_hash_inside", "[injamm][comment]") {
  BcUser data{"Alice", 30};
  auto bc = injamm::engine<BcUser>("Hello {{! has {# and #} inside }}{{name}}!");
  auto result = bc.render(data);
  REQUIRE(result.has_value());
  CHECK(*result == "Hello Alice!");
}

TEST_CASE("bang_comment_in_section", "[injamm][comment]") {
  BcUsersData data;
  data.users.push_back(BcUser{"a", 1});
  data.users.push_back(BcUser{"b", 2});
  auto bc = injamm::engine<BcUsersData>("{{#users}}{{! skip }}{{name}} {{/users}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  CHECK(*r == "a b ");
}

TEST_CASE("exists_section_present", "[injamm][exists]") {
  BcUsersData data;
  data.users.push_back(BcUser{"Alice", 30});
  auto bc = injamm::engine<BcUsersData>("{{#exists users}}yes{{/exists}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  CHECK(*r == "yes");
}

TEST_CASE("exists_section_missing", "[injamm][exists]") {
  BcUsersData data;
  auto bc = injamm::engine<BcUsersData>("{{#exists users}}yes{{/exists}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  CHECK(*r == "");
}

TEST_CASE("exists_inverted_present", "[injamm][exists]") {
  BcUsersData data;
  data.users.push_back(BcUser{"Alice", 30});
  auto bc = injamm::engine<BcUsersData>("{{^exists users}}no{{/exists}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  CHECK(*r == "");
}

TEST_CASE("exists_inverted_missing", "[injamm][exists]") {
  BcUsersData data;
  auto bc = injamm::engine<BcUsersData>("{{^exists users}}no{{/exists}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  CHECK(*r == "no");
}

TEST_CASE("if_eq_true", "[injamm][compare]") {
  BcUser data{"Alice", 30};
  auto bc = injamm::engine<BcUser>("{{#if age == 30}}match{{/if}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  CHECK(*r == "match");
}

TEST_CASE("if_eq_false", "[injamm][compare]") {
  BcUser data{"Alice", 30};
  auto bc = injamm::engine<BcUser>("{{#if age == 31}}match{{/if}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  CHECK(*r == "");
}

TEST_CASE("if_ne_true", "[injamm][compare]") {
  BcUser data{"Alice", 30};
  auto bc = injamm::engine<BcUser>("{{#if age != 31}}diff{{/if}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  CHECK(*r == "diff");
}

TEST_CASE("if_ne_false", "[injamm][compare]") {
  BcUser data{"Alice", 30};
  auto bc = injamm::engine<BcUser>("{{#if age != 30}}diff{{/if}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  CHECK(*r == "");
}

TEST_CASE("if_eq_with_else", "[injamm][compare]") {
  BcUser data{"Alice", 30};
  auto bc = injamm::engine<BcUser>("{{#if age == 30}}A{{else}}B{{/if}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  CHECK(*r == "A");
}

TEST_CASE("tilde_whitespace_trim_var", "[injamm][tilde]") {
  BcUser data{"Alice", 30};
  auto bc = injamm::engine<BcUser>("Hello {{~ name ~}}!");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  CHECK(*r == "Hello Alice!");
}

TEST_CASE("tilde_whitespace_trim_section", "[injamm][tilde]") {
  BcUsersData data;
  data.users.push_back(BcUser{"a", 1});
  data.users.push_back(BcUser{"b", 2});
  auto bc = injamm::engine<BcUsersData>("{{~#users~}}{{name}} {{~/users~}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  CHECK(*r == "a b ");
}

TEST_CASE("filter_replace_newlines", "[injamm][filter]") {
  BcUser data{"line1\nline2\nline3", 30};
  auto bc = injamm::engine<BcUser>("{{name|replace}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  CHECK(*r == "line1 line2 line3");
}

TEST_CASE("comment_multiline", "[injamm][comment]") {
  BcUser data{"Alice", 30};
  auto bc = injamm::engine<BcUser>("Hello {# multi\nline\ncomment #}{{name}}!");
  auto result = bc.render(data);
  REQUIRE(result.has_value());
  CHECK(*result == "Hello Alice!");
}

TEST_CASE("comment_between_literals", "[injamm][comment]") {
  BcUser data{"Alice", 30};
  auto bc = injamm::engine<BcUser>("Hello {# comment #}{{name}}!");
  auto result = bc.render(data);
  REQUIRE(result.has_value());
  CHECK(*result == "Hello Alice!");
}

TEST_CASE("comment_in_section", "[injamm][comment]") {
  BcUsersData data{{{"Alice", 30}, {"Bob", 25}}};
  auto bc = injamm::engine<BcUsersData>("{{#users}}{# comment #}{{name}}{{/users}}");
  auto result = bc.render(data);
  REQUIRE(result.has_value());
  CHECK(*result == "AliceBob");
}

TEST_CASE("comment_in_if_body", "[injamm][comment]") {
  BcIfData data{"test", 25};
  auto bc = injamm::engine<BcIfData>("{{#if age}}{# age is nonzero #}yes{{/if}}");
  auto result = bc.render(data);
  REQUIRE(result.has_value());
  CHECK(*result == "yes");
}

TEST_CASE("comment_ignore_inner_tags", "[injamm][comment]") {
  BcUser data{"Alice", 30};
  auto bc = injamm::engine<BcUser>("{{name}}{# {{age}} should be ignored #}!");
  auto result = bc.render(data);
  REQUIRE(result.has_value());
  CHECK(*result == "Alice!");
}

TEST_CASE("comment_multiple", "[injamm][comment]") {
  BcUser data{"Alice", 30};
  auto bc = injamm::engine<BcUser>("{#c1#}before{{name}}{#c2#}after");
  auto result = bc.render(data);
  REQUIRE(result.has_value());
  CHECK(*result == "beforeAliceafter");
}

