#include "injamm.hpp"
#include <glaze/glaze.hpp>
#include <catch2/catch_test_macros.hpp>
#include <climits>
#include <optional>
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
 * @details セクション内で {{@index}} が 0 から始まる連番を出力することを確認する。
 */
TEST_CASE("bc_at_index", "[injamm]") {
  BcUsersData data;
  data.users.push_back(BcUser{"a", 1});
  data.users.push_back(BcUser{"b", 2});
  data.users.push_back(BcUser{"c", 3});
  auto bc = injamm::engine<BcUsersData>("{{#users}}{{@index}}{{/users}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "012");
}

/**
 * @brief @first 特殊変数のテスト
 * @details セクション内で {{@first}} が先頭要素のみ "true"、
 *          それ以外は "false" を出力することを確認する。
 */
TEST_CASE("bc_at_first", "[injamm]") {
  BcUsersData data;
  data.users.push_back(BcUser{"a", 1});
  data.users.push_back(BcUser{"b", 2});
  auto bc = injamm::engine<BcUsersData>("{{#users}}{{@first}}{{/users}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "truefalse");
}

/**
 * @brief @last 特殊変数のテスト
 * @details セクション内で {{@last}} が末尾要素のみ "true"、
 *          それ以外は "false" を出力することを確認する。
 */
TEST_CASE("bc_at_last", "[injamm]") {
  BcUsersData data;
  data.users.push_back(BcUser{"a", 1});
  data.users.push_back(BcUser{"b", 2});
  auto bc = injamm::engine<BcUsersData>("{{#users}}{{@last}}{{/users}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "falsetrue");
}

/**
 * @brief {{#@first}} セクション構文のテスト
 * @details {{#@first}}...{{/@first}} で先頭要素のみ描画されることを確認する。
 */
TEST_CASE("bc_at_first_section", "[injamm]") {
  BcUsersData data;
  data.users.push_back(BcUser{"a", 1});
  data.users.push_back(BcUser{"b", 2});
  data.users.push_back(BcUser{"c", 3});
  auto bc = injamm::engine<BcUsersData>("{{#users}}{{#@first}}<{{name}}>{{/@first}}{{/users}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "<a>");
}

/**
 * @brief {{#@last}} セクション構文のテスト
 * @details {{#@last}}...{{/@last}} で末尾要素のみ描画されることを確認する。
 */
TEST_CASE("bc_at_last_section", "[injamm]") {
  BcUsersData data;
  data.users.push_back(BcUser{"a", 1});
  data.users.push_back(BcUser{"b", 2});
  auto bc = injamm::engine<BcUsersData>("{{#users}}{{#@last}}<{{name}}>{{/@last}}{{/users}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "<b>");
}

/**
 * @brief {{^@first}} 逆セクション構文のテスト
 * @details {{^@first}}...{{/@first}} で先頭要素以外を描画することを確認する。
 */
TEST_CASE("bc_at_first_inverted_section", "[injamm]") {
  BcUsersData data;
  data.users.push_back(BcUser{"a", 1});
  data.users.push_back(BcUser{"b", 2});
  data.users.push_back(BcUser{"c", 3});
  auto bc = injamm::engine<BcUsersData>("{{#users}}{{^@first}}<{{name}}>{{/@first}}{{/users}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "<b><c>");
}

/**
 * @brief {{^@last}} 逆セクション構文のテスト
 * @details {{^@last}}...{{/@last}} で末尾要素以外を描画することを確認する。
 */
TEST_CASE("bc_at_last_inverted_section", "[injamm]") {
  BcUsersData data;
  data.users.push_back(BcUser{"a", 1});
  data.users.push_back(BcUser{"b", 2});
  auto bc = injamm::engine<BcUsersData>("{{#users}}{{^@last}}<{{name}}>{{/@last}}{{/users}}");
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
    "users: {{#users}}{{name}} ({{age}}){{#if @last}}.{{else}}, {{/if}}{{/users}}");
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
 * @details セクション内で {{@last}} を if 条件に使用し、末尾要素のみドットを追記する動作を確認する。
 */
TEST_CASE("bc_if_with_at_last", "[injamm]") {
  auto bc = injamm::engine<BcUsersData>("{{#users}}{{name}}{{#if @last}}.{{/if}}{{/users}}");
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
  auto bc = injamm::engine<BcUsersData>("{{#users}}{{name}}{{#if @last}}.{{else}},{{/if}}{{/users}}");
  BcUsersData data{.users = {{"a", 1}, {"b", 2}, {"c", 3}}};
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "a,b,c.");
}

TEST_CASE("bc_at_root", "[injamm]") {
  BcRootData data;
  auto bc = injamm::engine<BcRootData>("app: {{@root}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "app: ");
}

TEST_CASE("bc_at_root_field_simple", "[injamm]") {
  BcRootData data;
  auto bc = injamm::engine<BcRootData>("{{@root.app_name}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "injamm");
}

TEST_CASE("bc_at_root_field_nested", "[injamm]") {
  BcRootData data;
  auto bc = injamm::engine<BcRootData>("{{@root.info.version}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "1.0");
}

TEST_CASE("bc_at_key_array", "[injamm]") {
  BcUsersData data;
  data.users.push_back(BcUser{"a", 1});
  data.users.push_back(BcUser{"b", 2});
  auto bc = injamm::engine<BcUsersData>("{{#users}}{{@key}}:{{name}},{{/users}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "0:a,1:b,");
}

TEST_CASE("bc_at_key_struct", "[injamm]") {
  BcMapWrapper data;
  auto bc = injamm::engine<BcMapWrapper>("{{#config}}{{@key}}={{this}};{{/config}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "host=localhost;port=8080;");
}

TEST_CASE("bc_struct_iteration_nested", "[injamm]") {
  BcMapWrapper data;
  auto bc = injamm::engine<BcMapWrapper>("{{#config}}{{#if @key}}k{{/if}}{{/config}}");
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
    "{{#if @first}}"
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
  auto bc = injamm::engine<BcUsersData>("{{#users}}{{#if @first}}skip{{/if}}{{#if @last}}last{{/if}}{{name}}|{{/users}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "skipAlice|Bob|lastCharlie|");
}

TEST_CASE("bc_break_with_if", "[break]") {
  BcUsersData data{.users = {{"Alice", 30}, {"Bob", 25}, {"Charlie", 35}, {"Dave", 40}}};
  auto bc = injamm::engine<BcUsersData>("{{#users}}{{#if @last}}{{#break}}{{/if}}{{name}}|{{/users}}");
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "Alice|Bob|Charlie|");
}

TEST_CASE("bc_continue_with_if", "[continue]") {
  BcUsersData data{.users = {{"Alice", 30}, {"Bob", 25}, {"Charlie", 35}, {"Dave", 40}}};
  auto bc = injamm::engine<BcUsersData>("{{#users}}{{#if @first}}{{#continue}}{{/if}}{{name}}|{{/users}}");
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

