# injamm ユースケースガイド

`injamm` は、C++20 以降の機能を活用した、高速で型安全なテンプレートエンジンです。Glaze ライブラリによるコンパイル時リフレクションを利用し、実行時のフィールドアクセスを O(1) で行う Bytecode VM と、コンパイル時にテンプレートを解析する NTTP レンダリングの 2 つの API を提供します。

本ガイドでは、`injamm` の主なユースケースと、具体的なコード例を紹介します。

---

## 1. Webアプリケーションの動的HTML生成

最も一般的なユースケースです。ユーザーリストや商品情報などのデータ構造を HTML テンプレートに埋め込みます。

### 機能ポイント
- **セクション (`{{#key}}`)**: 配列の反復処理。
- **条件分岐 (`{{#if}} / {{else}}`)**: データの有無や状態による表示の切り替え。
- **特殊変数 (`@index`, `@first`, `@last`)**: ループ内の位置に応じたスタイリング。

### コード例

```cpp
#include "injamm.hpp"
#include <iostream>
#include <vector>

struct Item {
  std::string name;
  int price;
  bool on_sale;
};

struct PageData {
  std::string title;
  std::vector<Item> items;
};

// Glaze メタデータ定義
template <> struct glz::meta<Item> {
  static constexpr auto value = glz::object("name", &Item::name, "price", &Item::price, "on_sale", &Item::on_sale);
};
template <> struct glz::meta<PageData> {
  static constexpr auto value = glz::object("title", &PageData::title, "items", &PageData::items);
};

int main() {
  PageData data{
    "Shopping List",
    {{"Apple", 100, true}, {"Banana", 150, false}, {"Cherry", 300, true}}
  };

  auto tmpl = R"(
    <h1>{{title}}</h1>
    <ul>
      {{#items}}
      <li class="{{#if @first}}first-item{{/if}}">
        {{name}}: {{price}}円
        {{#if on_sale}} <span class="badge">SALE!</span> {{/if}}
      </li>
      {{/items}}
    </ul>
  )";

  auto engine = injamm::engine<PageData>(tmpl);
  auto html = engine.render(data);

  if (html) std::cout << *html << std::endl;
}
```

---

## 2. データレポートとフォーマット（フィルタの活用）

数値をカンマ区切りにしたり、文字列を特定の幅に揃えたりするレポート出力に最適です。

### 機能ポイント
- **数値フィルタ (`numify`, `precision`, `zerofill`)**: 通貨や小数点以下のフォーマット。
- **文字列フィルタ (`upper`, `truncate`, `center`)**: 見出しの整形や長い文字列の省略。

### コード例

```cpp
#include "injamm.hpp"
#include <iostream>

struct Report {
  double revenue;
  int transaction_count;
  std::string summary;
};

template <> struct glz::meta<Report> {
  static constexpr auto value = glz::object(
    "revenue", &Report::revenue,
    "count", &Report::transaction_count,
    "summary", &Report::summary
  );
};

int main() {
  Report data{1234567.89, 42, "This is a very long monthly sales summary report that needs truncation."};

  // フィルタチェーンの例:
  // revenue | numify | precision(2) -> "1,234,567.89"
  // summary | truncate(20) | upper -> "THIS IS A VERY LO..."
  auto tmpl = R"(
    [Monthly Report]
    Revenue  : {{revenue | numify | precision(2)}} USD
    Count    : {{count | zerofill(5)}}
    Summary  : {{summary | truncate(30) | title}}
  )";

  auto engine = injamm::engine<Report>(tmpl);
  std::cout << *engine.render(data) << std::endl;
}
```

---

## 3. CLIツールのテーブル出力（位置合わせフィルタ）

CLIツールで、固定幅のテーブルを表示する場合に便利です。

### 機能ポイント
- **アライメントフィルタ (`left`, `right`, `center`)**: 文字列をスペースでパディング。
- **`@index`**: 行番号の表示。

### コード例

```cpp
auto tmpl = R"(
ID   | Name                 | Status
-----|----------------------|-------
{{#users}}
{{@index | zerofill(4)}} | {{name | right(20)}} | {{status | center(7)}}
{{/users}}
)";

// 実行結果例:
// 0000 |                Alice |  READY
// 0001 |                  Bob |  BUSY
```

---

## 4. 設定ファイル（JSON/YAML/SQL）の自動生成

構造化されたデータから、特定の設定ファイルやSQLクエリを生成します。

### 機能ポイント
- **`{{{key}}}` (Raw出力)**: HTMLエスケープを行わずに文字列をそのまま出力。
- **Nested Path**: `{{user.address.city}}` のような深い構造へのアクセス。
- **`{{#break}}` / `{{#continue}}`**: 条件に応じたループ制御。

### コード例

```cpp
auto sql_tmpl = R"(
INSERT INTO users (name, age, city) VALUES
{{#users}}
('{{{name}}}', {{age}}, '{{{address.city}}}'){{#if @last}};{{else}},{{/if}}
{{/users}}
)";
```

---

## 5. 超高速な定型文生成（コンパイル時レンダリング）

実行時のオーバーヘッドを極限まで減らしたい場合、C++の NTTP (Non-Type Template Parameter) を利用したコンパイル時解析機能が使えます。

### 特徴
- テンプレートのパースをコンパイル時に完了。
- 変数置換のみの単純なテンプレート（通知メッセージ、ログなど）に最適。

### コード例

```cpp
#include "injamm.hpp"
#include <iostream>

struct LogEvent {
  std::string level;
  int code;
};

template <> struct glz::meta<LogEvent> {
  static constexpr auto value = glz::object("level", &LogEvent::level, "code", &LogEvent::code);
};

int main() {
  // テンプレートを NTTP として定義
  auto constexpr kLogTmpl = injamm::fixed_string("[{{level}}] Error occurred (Code: {{code}})");

  LogEvent ev{"CRITICAL", 500};

  // コンパイル時にパース済み。実行時は置換のみ。
  auto msg = injamm::render<kLogTmpl>(ev);
  if (msg) std::cout << *msg << std::endl;
}
```

---

## 6. 付録：フィルタ・特殊変数リファレンス

### 特殊変数
| 変数名 | 説明 |
| :--- | :--- |
| `{{@index}}` | ループの現在のインデックス (0始まり) |
| `{{@first}}` | ループの最初の要素であれば `true` |
| `{{@last}}` | ループの最後の要素であれば `true` |
| `{{@key}}` | Mapの反復時に現在のキー、または配列のインデックス |
| `{{@root}}` | ルートオブジェクト全体のシリアライズ |
| `{{this}}` | 現在のコンテキストオブジェクト自体のシリアライズ |
| `{{#break}}` | ループを途中で終了 |
| `{{#continue}}` | 現在の要素をスキップして次へ |

### 主なフィルタ
- **文字列**: `upper`, `lower`, `trim`, `truncate(n)`, `left(n)`, `right(n)`, `center(n)`, `substr(start, len)`
- **整数**: `abs`, `hex`, `oct`, `bin`, `numify` (カンマ区切り), `zerofill(n)`, `mod(n)`, `eq(n)`
- **実数**: `precision(n)`
