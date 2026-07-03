# injamm テンプレート構文リファレンス

本ドキュメントでは injamm が提供するすべてのテンプレート構文を、説明・サンプルコード・出力結果とともに網羅します。

**凡例:** 以下のサンプルで使用するデータ型:

```cpp
struct User {
  std::string name;
  int age{};
  bool active{};
};

struct Data {
  std::vector<User> users;
  std::string title;
};

// メタデータの定義はなくてもよいです
template <> struct glz::meta<User> {
  static constexpr auto value = glz::object("name", &User::name, "age", &User::age, "active", &User::active);
};
template <> struct glz::meta<Data> {
  static constexpr auto value = glz::object("users", &Data::users, "title", &Data::title);
};
```

---

## 1. 変数出力

### 1.1 HTML エスケープあり (`{{var}}`)

HTML 特殊文字 (`<`, `>`, `&`, `"`, `'`) をエスケープして出力します。

| テンプレート | データ                   | 出力                     |
| ------------ | ------------------------ | ------------------------ |
| `{{name}}`   | `User{"<b>Bob</b>", 30}` | `&lt;b&gt;Bob&lt;/b&gt;` |

```cpp
Data data{{{"Alice", 30}, {"Bob", 25}}, "Members"};
auto r = injamm::render<injamm::fixed_string("{{title}}")>(data);
// r == "Members"
```

### 1.2 生出力 / エスケープなし (`{{{var}}}`)

値をそのまま出力します（HTML エスケープなし）。ステンシルモード用。

| テンプレート | データ                   | 出力         |
| ------------ | ------------------------ | ------------ |
| `{{{name}}}` | `User{"<b>Bob</b>", 30}` | `<b>Bob</b>` |

```cpp
auto bc = injamm::engine<User>("{{{name}}}");
auto r = bc.render(User{"<b>Alice</b>", 30});
// r == "<b>Alice</b>"
```

### 1.3 複数変数

```cpp
auto bc = injamm::engine<User>("{{name}}: {{age}}");
auto r = bc.render(User{"Alice", 30});
// r == "Alice: 30"
```

### 1.4 コンテナのサイズ (`{{field.size}}`)

ベクトル・マップ・セットなどコンテキストフィールドの要素数を出力します。`std::size_t` として出力されるため、数値フィルターとの組み合わせも可能です。

```cpp
struct Data {
  std::vector<User> users;
};

// engine
auto bc = injamm::engine<Data>("Total: {{users.size}} users");
auto r = bc.render(Data{{{"Alice", 30}, {"Bob", 25}}});
// r == "Total: 2 users"

// NTTP
auto r2 = injamm::render<injamm::fixed_string("Count: {{users.size}}")>(
  Data{{{"Alice", 30}, {"Bob", 25}}});
// r2 == "Count: 2"

// 空コンテナ
auto r3 = injamm::render<injamm::fixed_string("{{users.size}}")>(Data{});
// r3 == "0"
```

---

## 2. ネストパス (`{{foo.bar.baz}}`)

ドット区切りで構造体のネストフィールドにアクセスできます。

```cpp
struct Address { std::string city; std::string country; };
struct Person  { std::string name; Address addr; };

template <> struct glz::meta<Address> {
  static constexpr auto value = glz::object("city", &Address::city, "country", &Address::country);
};
template <> struct glz::meta<Person> {
  static constexpr auto value = glz::object("name", &Person::name, "addr", &Person::addr);
};

auto bc = injamm::engine<Person>("{{addr.city}}, {{addr.country}}");
auto r = bc.render(Person{"Alice", {"NYC", "USA"}});
// r == "NYC, USA"
```

---

## 3. ルートコンテキストアクセス (`{{root.field}}`)

ルートコンテキストのフィールドに直接アクセスします。セクション（ループ）内から外側のデータを参照する際に使用します。

| テンプレート     | 出力      |
| ---------------- | --------- |
| `{{root.title}}` | `Members` |

```cpp
// ループ内からルートの title を参照
auto r = injamm::render<injamm::fixed_string(
  "{{#users}}{{name}} ({{root.title}}){{/users}}")>(Data{{{"Alice", 30}, {"Bob", 25}}, "Members"});
// r == "Alice (Members)Bob (Members)"
```

ネストしたルートフィールドも参照可能:

```cpp
struct RootData {
  std::string app_name;
  struct Inner { std::string version; } info;
};
// {{root.app_name}} → "injamm"
// {{root.info.version}} → "1.0"
```

> **注意:** `{{@root.field}}`（`@` 付き）は旧構文です。新しいコードでは `{{root.field}}`（`@` なし）を使用してください。

---

## 4. 現在コンテキストの出力 (`{{this}}`)

現在のコンテキスト値をそのままシリアライズして出力します。主に `std::map` や構造体の全フィールド反復で値を取得するために使用します。

```cpp
struct MapData {
  std::map<std::string, int> scores{{"alice", 95}, {"bob", 87}};
};
// {{#scores}}{{loop.key}}={{this}} {{/scores}}
// → "alice=95 bob=87 "
```

---

## 5. セクション（ループ / 条件付きブロック）

### 5.1 基本セクション (`{{#section}}...{{/section}}`)

配列の場合は各要素に対してボディを繰り返し描画します。真偽値/数値/文字列の場合は真と評価されれば一度だけ描画します。

**真理値判定:**
- 非ゼロ数値 → 真
- 非空文字列 → 真
- `true` → 真
- 非ヌルポインタ → 真
- 空配列/空マップ → 偽
- `0` / `false` / 空文字列 → 偽

```cpp
// 配列ループ
auto r = injamm::render<injamm::fixed_string(
  "{{#users}}{{name}}({{age}}) {{/users}}")>(Data{{{"Alice", 30}, {"Bob", 25}}, ""});
// r == "Alice(30) Bob(25) "

// 真偽値セクション
struct BoolData { bool flag; };
auto bc = injamm::engine<BoolData>("{{#flag}}active{{/flag}}");
bc.render(BoolData{true});   // r == "active"
bc.render(BoolData{false});  // r == ""
```

### 5.2 逆セクション (`{{^section}}...{{/section}}`)

セクションが偽/空のときにボディを描画します。

```cpp
auto bc = injamm::engine<BoolData>("{{^flag}}inactive{{/flag}}");
bc.render(BoolData{false}); // r == "inactive"
```

### 5.3 存在チェック (`{{#exists var}}...{{/exists}}`)

変数が存在し、かつ真と評価できる場合に描画します。`{{^exists var}}` で否定形。

```cpp
auto bc = injamm::engine<User>("{{#exists name}}{{name}}{{/exists}}");
// name が存在すれば描画、空文字列なら描画しない
```

### 5.4 ネストセクション

セクションは入れ子にできます。内側のセクションは外側のループごとに評価されます。

```cpp
struct Company {
  std::string name;
  std::vector<Person> employees;
};
// {{#employees}}{{name}}@{{company_name}}{{/employees}}
```

---

## 6. 条件分岐

### 6.1 if (`{{#if cond}}...{{/if}}`)

条件式が真の場合にボディを描画します。セクションと同じ真理値判定を用います。

```cpp
auto bc = injamm::engine<User>("{{#if age}}adult{{/if}}");
bc.render(User{"Alice", 20}); // r == "adult"
bc.render(User{"Bob", 0});    // r == ""
```

### 6.2 if/else (`{{#if cond}}...{{else}}...{{/if}}`)

```cpp
auto r = injamm::render<injamm::fixed_string(
  "{{#if age}}{{name}} is adult{{else}}{{name}} is minor{{/if}}")>(User{"Bob", 0});
// r == "Bob is minor"
```

### 6.3 直接比較 (`{{#if lhs op rhs}}...{{/if}}`)

`==`, `!=`, `<`, `<=`, `>`, `>=` をサポートします。右辺には整数リテラル、文字列リテラル、または別の変数を指定できます。

```cpp
auto bc = injamm::engine<User>("{{#if age == 18}}exactly 18{{/if}}");
bc.render(User{"Alice", 18}); // r == "exactly 18"

// !=
auto r = injamm::render<injamm::fixed_string(
  "{{#if age != 0}}non-zero{{/if}}")>(User{"Alice", 25});
// r == "non-zero"

// 直接大小比較
auto r2 = injamm::render<injamm::fixed_string(
  "{{#if age >= 18}}adult{{else}}minor{{/if}}")>(User{"Alice", 25});
// r2 == "adult"

// 文字列リテラル比較
auto r3 = injamm::render<injamm::fixed_string(
  "{{#if name == \"Alice\"}}match{{/if}}")>(User{"Alice", 25});
// r3 == "match"
```

別の変数との比較:

```cpp
struct Pair {
  int lhs{};
  int rhs{};
};

template <> struct glz::meta<Pair> {
  static constexpr auto value = glz::object("lhs", &Pair::lhs, "rhs", &Pair::rhs);
};

auto bc = injamm::engine<Pair>("{{#if lhs > rhs}}gt{{/if}}");
bc.render(Pair{10, 3}); // r == "gt"
```

### 6.4 論理演算 (`{{#if a || b}}`, `{{#if a && b}}`, `{{#if !a}}`)

単純な論理演算として `||`, `&&`, `!` をサポートします。各オペランドの真偽値判定は通常の `if` と同じです。

```cpp
struct Flags {
  bool a{};
  bool b{};
};

template <> struct glz::meta<Flags> {
  static constexpr auto value = glz::object("a", &Flags::a, "b", &Flags::b);
};

auto r = injamm::render<injamm::fixed_string(
  "{{#if a || b}}on{{/if}}")>(Flags{false, true});
// r == "on"

auto r2 = injamm::render<injamm::fixed_string(
  "{{#if !a}}off{{/if}}")>(Flags{false, false});
// r2 == "off"
```

### 6.5 if + フィルター

フィルターを使って条件判定することもできます。`is_neg`, `eq(n)`, `ne(n)`, `gt(n)`, `gte(n)`, `lt(n)`, `lte(n)` フィルターが特に有用です。

```cpp
auto r = injamm::render<injamm::fixed_string(
  "{{#if age | gt(18)}}adult{{else}}minor{{/if}}")>(User{"Alice", 25});
// r == "adult"

// フィルターチェーン: age を 4 で割った余りが 2 と等しい
auto r2 = injamm::render<injamm::fixed_string(
  "{{#if age | mod(4) | eq(2)}}match{{/if}}")>(User{"Alice", 10});
// r2 == "match"

// is_neg
auto bc = injamm::engine<User>("{{#if age | is_neg}}negative{{else}}non-negative{{/if}}");
bc.render(User{"Alice", -5}); // r == "negative"
```

---

## 7. ループ制御

### 7.1 Break (`{{#break}}`)

ループを中断します。

```cpp
// 最初の要素でのみ break
auto r = injamm::render<injamm::fixed_string(
  "{{#users}}{{name}}{{#if loop.is_last}}.{{else}}{{#break}}{{/if}}{{/users}}")>(
  Data{{{"Alice", 30}, {"Bob", 25}, {"Charlie", 35}}, ""});
// r == "Alice"
```

### 7.2 Continue (`{{#continue}}`)

現在の反復をスキップし、次の要素へ進みます。

```cpp
// {{loop.index}} が真（≠0）の要素をスキップ → 最初の要素のみ描画
auto r = injamm::render<injamm::fixed_string(
  "{{#users}}{{#if loop.index}}{{#continue}}{{/if}}{{name}}{{/users}}")>(
  Data{{{"Alice", 30}, {"Bob", 25}}, ""});
// r == "Alice"
```

---

## 8. ループ変数

セクション（配列ループ）内でのみ有効な特殊変数です。

| 変数                | 説明                         | 例               |
| ------------------- | ---------------------------- | ---------------- |
| `{{loop.index}}`    | 0 始まりのインデックス       | `0`, `1`, `2`    |
| `{{loop.index1}}`   | 1 始まりのインデックス       | `1`, `2`, `3`    |
| `{{loop.size}}`     | ループの総要素数             | `3`              |
| `{{loop.is_first}}` | 最初の要素なら `true`        | `true` / `false` |
| `{{loop.is_last}}`  | 最後の要素なら `true`        | `true` / `false` |
| `{{loop.key}}`      | 現在のキー名（マップ反復時） | `"alice"`        |
| `{{loop.parent.*}}` | 親ループの loop 変数         | `{{loop.parent.index}}` |

```cpp
// ループ変数の使用例
auto r = injamm::render<injamm::fixed_string(
  "{{#users}}{{loop.index}}:{{name}}{{#if loop.is_last}}.{{else}}, {{/if}}{{/users}}")>(
  Data{{{"Alice", 30}, {"Bob", 25}, {"Charlie", 35}}, ""});
// r == "0:Alice, 1:Bob, 2:Charlie."
```

ループ変数をセクションキーとしても使用できます:

親ループにも `parent` を連結してアクセスできます。

```cpp
struct Group {
  std::vector<int> items;
};
struct Nested {
  std::vector<Group> groups;
};

template <> struct glz::meta<Group> {
  static constexpr auto value = glz::object("items", &Group::items);
};
template <> struct glz::meta<Nested> {
  static constexpr auto value = glz::object("groups", &Nested::groups);
};

auto r = injamm::render<injamm::fixed_string(
  "{{#groups}}{{#items}}({{loop.parent.index}}/{{loop.index}}={{this}}){{/items}}{{/groups}}")>(
  Nested{{{{1, 2}}, {{3}}}});
// r == "(0/0=1)(0/1=2)(1/0=3)"
```

### 8.1 `{{#loop.is_first}}...{{/loop.is_first}}`

先頭要素のみ描画します。

```cpp
auto r = injamm::render<injamm::fixed_string(
  "{{#users}}{{#loop.is_first}}<{{name}}>{{/loop.is_first}}{{/users}}")>(
  Data{{{"Alice", 30}, {"Bob", 25}, {"Charlie", 35}}, ""});
// r == "<Alice>"
```

### 8.2 `{{#loop.is_last}}...{{/loop.is_last}}`

末尾要素のみ描画します。

```cpp
auto r = injamm::render<injamm::fixed_string(
  "{{#users}}{{name}}{{#loop.is_last}}.{{else}}, {{/loop.is_last}}{{/users}}")>(
  Data{{{"Alice", 30}, {"Bob", 25}}, ""});
// r == "Alice, Bob."
```

### 8.3 `{{^loop.is_first}}...{{/^loop.is_first}}`

先頭以外の要素を描画します。

```cpp
auto r = injamm::render<injamm::fixed_string(
  "{{#users}}{{^loop.is_first}}, {{/^loop.is_first}}{{name}}{{/users}}")>(
  Data{{{"Alice", 30}, {"Bob", 25}, {"Charlie", 35}}, ""});
// r == "Alice, Bob, Charlie"
```

### 8.4 `{{^loop.is_last}}...{{/^loop.is_last}}`

末尾以外の要素を描画します。

```cpp
auto r = injamm::render<injamm::fixed_string(
  "{{#users}}{{name}}{{^loop.is_last}}, {{/^loop.is_last}}{{/users}}")>(
  Data{{{"Alice", 30}, {"Bob", 25}}, ""});
// r == "Alice, Bob."
```

---

## 9. コメント

### 9.1 Mustache スタイル (`{{! comment }}`)

テンプレートから取り除かれ、出力には含まれません。

```cpp
auto bc = injamm::engine<User>("Hello{{! this is a comment }} World!");
auto r = bc.render(User{"Alice", 30});
// r == "Hello World!"
```

### 9.2 injamm スタイル (`{# comment #}`)

`{{! }}` とは異なる構文です。`{#` と `#}` で囲みます。`{{#` タグとの混同を避けるため、`{` の直後に `#` が来た場合のみ認識されます。

```cpp
auto bc = injamm::engine<User>("Hello{# this is a comment #} World!");
// r == "Hello World!"
```

---

## 10. @var 定数置換 (`{{@var(name)}}`)

テンプレートをパースする**前**にプリプロセッサ的に展開されます。フィールド名のエイリアスやセクションキーのパラメータ化に使用します。

### 10.1 Runtime (engine)

```cpp
std::map<std::string, std::string, std::less<>> consts{
  {"f", "name"},
  {"s", "items"}
};
auto bc = injamm::engine<Data>("{{#@var(s)}}{{@var(f)}}{{/@var(s)}}", consts);
// 内部的に "{{#items}}{{name}}{{/items}}" としてコンパイル
auto r = bc.render(Data{{{"Alice"}, {"Bob"}}, ""});
// r == "AliceBob"
```

### 10.2 NTTP (render)

```cpp
// テンプレート引数で key, value を交互に渡す
auto r = injamm::render<"{{@var(f)}} | upper", "f", "name">(User{"alice", 25});
// コンパイル時に "{{name | upper}}" に展開
// r == "ALICE"
```

**再帰展開:** 値に `@var(yyy)` が含まれている場合は再帰的に展開します。

---

## 11. ホワイトスペース制御

### 11.1 `{{~ var ~}}` — タグ前後の空白トリム

`~` をタグの内側先頭/末尾に置くと、タグ直前/直後の空白行をトリムします。

```cpp
auto bc = injamm::engine<User>("  {{~name~}}  ");
auto r = bc.render(User{"Alice", 30});
// r == "Alice"
```

### 11.2 trim_blocks / lstrip_blocks

`engine<T>` の第3・第4引数で制御します。

```cpp
// trim_blocks: }} の直後の改行を除去
auto bc = injamm::engine<User>("a{{name}}\nb", true);  // trim_blocks=true
auto r = bc.render(User{"Alice", 30});
// r == "aAliceb"

// lstrip_blocks: ブロックタグ({{#}}/{{^}}/{{/}})の直前の空白を除去
auto bc2 = injamm::engine<User>("a\n  {{#active}}y{{/active}}", false, true);  // lstrip_blocks=true
// r == "a\ny"
```

---

## 12. 文字列フィルター

| フィルター    | 説明                  | 入力            | 出力            |
| ------------- | --------------------- | --------------- | --------------- |
| `upper`       | ASCII 小文字→大文字   | `"hello"`       | `"HELLO"`       |
| `lower`       | ASCII 大文字→小文字   | `"HELLO"`       | `"hello"`       |
| `capitalize`  | 先頭の文字を大文字    | `"hello"`       | `"Hello"`       |
| `title`       | 各単語の先頭を大文字  | `"hello world"` | `"Hello World"` |
| `trim`        | 前後の空白を除去      | `"  hi  "`      | `"hi"`          |
| `ltrim`       | 先頭の空白を除去      | `"  hi"`        | `"hi"`          |
| `rtrim`       | 末尾の空白を除去      | `"hi  "`        | `"hi"`          |
| `left(n)`     | n 文字枠に左寄せ      | `"hi"`          | `"        hi"`  |
| `right(n)`    | n 文字枠に右寄せ      | `"hi"`          | `"hi        "`  |
| `center(n)`   | n 文字枠に中央寄せ    | `"hi"`          | `"   hi    "`   |
| `truncate(n)` | n 字超を n-3 字+`...` | `"hello world"` | `"hello wo..."` |
| `substr(n)`   | n 文字目から末尾まで  | `"hello"`       | `"llo"`         |
| `substr(n,m)` | n 文字目から m 文字   | `"hello"`       | `"el"`          |
| `replace`     | 改行を空白に置換      | `"a\nb"`        | `"a b"`         |

```cpp
auto r = injamm::render<injamm::fixed_string("{{name | upper}}")>(User{"hello", 0});
// r == "HELLO"

auto bc = injamm::engine<User>("{{name | left(10) | upper}}");
auto r2 = bc.render(User{"hi", 0});
// r2 == "HI        "
```

---

## 13. 整数フィルター

| フィルター    | 説明              | 入力      | 出力        |
| ------------- | ----------------- | --------- | ----------- |
| `abs`         | 絶対値            | `-42`     | `42`        |
| `neg`         | 符号反転          | `42`      | `-42`       |
| `hex`         | 16進数            | `255`     | `ff`        |
| `oct`         | 8進数             | `64`      | `100`       |
| `bin`         | 2進数             | `5`       | `101`       |
| `mod(n)`      | n で割った余り    | `10`      | `2`         |
| `numify`      | 3桁カンマ区切り   | `1234567` | `1,234,567` |
| `zerofill(n)` | n 桁ゼロ埋め      | `42`      | `00042`     |
| `is_neg`      | 負数判定          | `-5`      | `true`      |
| `eq(n)`       | n と等しい        | `25`      | `true`      |
| `ne(n)`       | n と異なる        | `25`      | `false`     |
| `gt(n)`       | n より大きい      | `25`      | `true`      |
| `gte(n)`      | n 以上            | `18`      | `true`      |
| `lt(n)`       | n 未満            | `5`       | `true`      |
| `lte(n)`      | n 以下            | `5`       | `true`      |
| `add(n)`      | n を加算          | `10`      | `13`        |
| `sub(n)`      | n を減算          | `10`      | `7`         |
| `mul(n)`      | n を乗算          | `10`      | `30`        |
| `div(n)`      | n で除算（切捨）  | `10`      | `3`         |

```cpp
auto r = injamm::render<injamm::fixed_string("{{age | numify}}")>(User{"Alice", 1234567});
// r == "1,234,567"

auto bc = injamm::engine<User>("{{age | zerofill(5)}}");
auto r2 = bc.render(User{"Alice", 42});
// r2 == "00042"

// if 条件と組み合わせ
auto r3 = injamm::render<injamm::fixed_string(
  "{{#if age | eq(18)}}exactly 18{{/if}}")>(User{"Alice", 18});
// r3 == "exactly 18"

// 四則演算フィルター
auto r4 = injamm::render<injamm::fixed_string("{{age | add(10)}}")>(User{"Alice", 25});
// r4 == "35"

auto r5 = injamm::render<injamm::fixed_string("{{age | sub(3)}}")>(User{"Alice", 25});
// r5 == "22"

auto r6 = injamm::render<injamm::fixed_string("{{age | mul(2)}}")>(User{"Alice", 25});
// r6 == "50"

auto r7 = injamm::render<injamm::fixed_string("{{age | div(3)}}")>(User{"Alice", 25});
// r7 == "8"  (切捨て除算)

// フィルターチェーンで演算を連結
auto r8 = injamm::render<injamm::fixed_string("{{age | add(5) | mul(2)}}")>(User{"Alice", 25});
// r8 == "60"  ((25+5)*2)
```

---

## 14. 浮動小数点フィルター

| フィルター     | 説明                  | 入力      | 出力   |
| -------------- | --------------------- | --------- | ------ |
| `precision(n)` | 小数点以下 n 桁に丸め | `3.14159` | `3.14` |

```cpp
struct FloatData { double value; };
template <> struct glz::meta<FloatData> {
  static constexpr auto value = glz::object("value", &FloatData::value);
};

auto r = injamm::render<injamm::fixed_string("{{value | precision(2)}}")>(
  FloatData{3.14159});
// r == "3.14"
```

---

## 15. フィルターチェーン

複数のフィルターを `|` で連結できます。左から右に順に適用されます。

```cpp
// 文字列フィルターチェーン
auto bc = injamm::engine<User>("{{name | trim | upper}}");
bc.render(User{"  hello  ", 0});  // r == "HELLO"

// 文字列 + パディング
bc = injamm::engine<User>("{{name | left(10) | upper}}");
bc.render(User{"hi", 0});          // r == "HI        "

// 整数フィルターチェーン
bc = injamm::engine<User>("{{age | abs | numify}}");
bc.render(User{"Alice", -1234567});// r == "1,234,567"

// if + フィルターチェーン
auto r = injamm::render<injamm::fixed_string(
  "{{#if age | mod(4) | ne(0)}}not_divisible_by_4{{/if}}")>(User{"Alice", 10});
// r == "not_divisible_by_4"
```

1つのプレースホルダに適用できるフィルターは最大 **4 つ**です。

---

## 16. std::map 反復

`std::map` をセクションとして反復できます。キーは `{{loop.key}}`、値はフィールド名または `{{this}}` でアクセスします。

```cpp
struct MapData {
  std::map<std::string, int> values{{"x", 10}, {"y", 20}};
};

auto r = injamm::render<injamm::fixed_string(
  "{{#values}}{{loop.key}}={{this}} {{/values}}")>(MapData{});
// r == "x=10 y=20 "
```

構造体の値を持つマップ:

```cpp
struct Item { std::string name; int score{}; };
struct MapStructData {
  std::map<std::string, Item> items{{"a", {"Alice", 95}}, {"b", {"Bob", 87}}};
};

auto r = injamm::render<injamm::fixed_string(
  "{{#items}}{{loop.key}}:{{name}}={{score}} {{/items}}")>(MapStructData{});
// r == "a:Alice=95 b:Bob=87 "
```

マップが空の場合はセクションは描画されず、逆セクションが描画されます。

`std::unordered_map` も同様に反復可能です（順序は不定）。

---

## 17. 構造体フィールド反復

Glaze リフレクションにより、構造体のフィールドをマップのように反復できます。

```cpp
struct Config {
  std::string host{"localhost"};
  int port{8080};
};

auto r = injamm::render<injamm::fixed_string(
  "{{#config}}{{loop.key}}={{this}};{{/config}}")>(Config{});
// r == "host=localhost;port=8080;"
```

---

## 18. `bind()` で複数コンテキストを束ねる

`injamm::bind<Names...>(values...)` を使うと、独立した値やコンテナを 1 つのレンダリングコンテキストとして扱えます。`render`（NTTP）で特に便利です。

```cpp
std::vector<User> users{{"Alice", 30, true}, {"Bob", 25, false}};
std::string title = "Members";

auto ctx = injamm::bind<"title", "users">(title, users);
auto r = injamm::render<injamm::fixed_string(
  "{{title}}: {{#users}}{{name}} {{/users}}")>(ctx);
// r == "Members: Alice Bob "
```

単一の値なら `bind(value)` で `"_"` 名が自動的に付きます。

```cpp
auto r = injamm::render<injamm::fixed_string("Got {{_}}")>(injamm::bind(42));
// r == "Got 42"
```

---

## 19. Template Partials（`engine<T>` 専用）

同一テンプレート内で名前付きの再利用可能な断片（partial）を定義できます。
`render(data, "partial_name")` で特定の partial だけを選択的にレンダリングすることも可能です（HTMX 等の部分更新に有用）。

partial の本体は **プリコンパイル方式**で保持されるため、呼び出しのたびに再コンパイルは発生しません。

### 19.1 定義と呼び出し

`{{#partialdef name}}...{{/partialdef}}` で定義し、`{{#partial name}}` で呼び出します。

```cpp
struct Data {
  std::string title;
  std::string name;
  std::vector<std::string> items;
};

auto bc = injamm::engine<Data>(R"(
<h1>{{title}}</h1>
{{#partialdef sidebar}}
<div class="sidebar">
  <h3>{{name}}'s items</h3>
  <ul>{{#items}}<li>{{this}}</li>{{/items}}</ul>
</div>
{{/partialdef}}
<main>
  {{#partial sidebar}}
  {{#partial sidebar}}
</main>
)");

auto r = bc.render(data);
// r == "<h1>Members</h1>\n... (両方の sidebar が描画される)"
```

### 19.2 複数回呼び出し

同じ partial を同一テンプレート内で何度でも呼び出せます。呼び出しのたびに現在のコンテキストで評価されます。

```cpp
struct User { std::string name; int age; };
auto eng = injamm::engine<User>(
  "{{#partialdef card}}{{name}}({{age}}){{/partialdef}}"
  "{{#partial card}} {{#partial card}} {{#partial card}}");
auto r = eng.render(User{"Alice", 30});
// r == "Alice(30) Alice(30) Alice(30)"
```

### 19.3 ループ内での使用

セクション（ループ）内で partial を呼び出すと、現在のループ要素をコンテキストとして描画されます。

```cpp
struct Data { std::vector<User> users; };
auto eng = injamm::engine<Data>(
  "{{#partialdef card}}{{name}}({{age}})/{{/partialdef}}"
  "{{#users}}{{#partial card}}{{/users}}");
auto r = eng.render(Data{{{"Bob", 25}, {"Charlie", 35}}});
// r == "Bob(25)/Charlie(35)/"
```

### 19.4 前方参照

partial の定義より前の位置で呼び出せます（前方参照可能）。

```cpp
auto eng = injamm::engine<User>(
  "before {{#partial info}} after"
  "{{#partialdef info}}{{name}}({{age}}){{/partialdef}}");
auto r = eng.render(User{"Bob", 25});
// r == "before Bob(25) after"
```

### 19.5 入れ子 partial

partial の中から別の partial を呼び出せます。

```cpp
struct Data { std::vector<User> users; };
auto eng = injamm::engine<Data>(
  "{{#partialdef name}}{{name}}{{/partialdef}}"
  "{{#partialdef card}}{{#partial name}}({{age}})/{{/partialdef}}"
  "{{#users}}{{#partial card}}{{/users}}");
auto r = eng.render(Data{{{"Bob", 25}, {"Charlie", 35}}});
// r == "Bob(25)/Charlie(35)/"
```

### 19.6 部分レンダリング

`engine.render(data, "partial_name")` でテンプレート全体ではなく、特定の partial のみをレンダリングできます。

```cpp
auto eng = injamm::engine<User>(
  "{{#partialdef sidebar}}{{name}}'s page{{/partialdef}}"
  "full: {{#partial sidebar}}|end");
auto full = eng.render(User{"Dave", 40});
// full == "full: Dave's page|end"

auto side = eng.render(User{"Dave", 40}, "sidebar");
// side == "Dave's page"
```

存在しない partial 名を指定すると `unknown_key` エラーを返します。

### 19.7 制限

- `{{#partialdef}}` のネストはできません
- NTTP `render<fixed_string>` では未対応です（runtime `engine<T>` のみ）
- ファイルインクルード（`{% include %}`）には対応していません

---

## 20. partial / include（NTTP render 専用）

`{{> partial_name}}` は NTTP `render` の compile-time entry pair で展開されます。runtime `engine<T>` では未対応です。

```cpp
std::string title = "Members";
std::string name = "Alice";

auto ctx = injamm::bind<"title", "name">(title, name);
auto r = injamm::render<
  "{{> header}} {{name}} {{> footer}}",
  "header", "<h1>{{title}}</h1>",
  "footer", "<footer>{{title}}</footer>"
>(ctx);
// r == "<h1>Members</h1> Alice <footer>Members</footer>"
```

partial の中には通常のテンプレート構文を書けます。展開はパース前に行われます。

---

## エラーコード

| 値  | 名前               | 意味                       |
| --- | ------------------ | -------------------------- |
| 0   | `none`             | エラーなし                 |
| 1   | `no_read_input`    | 入力が空                   |
| 2   | `unexpected_end`   | 予期しないテンプレート終端 |
| 3   | `unknown_key`      | 不明なキー                 |
| 4   | `syntax_error`     | 構文エラー                 |
| 5   | `type_mismatch`    | 型不一致                   |
| 6   | `invalid_utf8`     | 不正な UTF-8               |
| 7   | `unknown_filter`   | 不明なフィルター名         |
| 8   | `division_by_zero` | 除数ゼロエラー             |

---

## API クイックリファレンス

```cpp
// NTTP コンパイル時レンダリング（推奨）
constexpr auto kTmpl = injamm::fixed_string("Hello {{name}}!");
auto r = injamm::render<kTmpl>(User{"Bob"});

// @var 定数置換付き NTTP
auto r2 = injamm::render<"{{@var(f)}} | upper", "f", "name">(User{"alice"});

// バイトコード VM（テンプレートが動的な場合）
auto bc = injamm::engine<User>("{{name}} ({{age}})");
auto r3 = bc.render(User{"Alice", 30});

// @var 定数置換付き engine
std::map<std::string, std::string, std::less<>> c{{"f", "name"}};
auto bc2 = injamm::engine<User>("{{@var(f)}}", c);

// trim_blocks / lstrip_blocks
auto bc3 = injamm::engine<User>("a\n{{name}}\nb", true, false);
```
