# injamm ユースケースガイド

`injamm` は、C++20 以降の機能を活用した、高速で型安全なテンプレートエンジンです。Glaze ライブラリによるコンパイル時リフレクションを利用し、実行時のフィールドアクセスを O(1) で行う Bytecode VM と、コンパイル時にテンプレートを解析する NTTP レンダリングの 2 つの API を提供します。

本ガイドでは、`injamm` の主なユースケースと、具体的なコード例を紹介します。

---

## 1. Webアプリケーションの動的HTML生成

最も一般的なユースケースです。ユーザーリストや商品情報などのデータ構造を HTML テンプレートに埋め込みます。

### 機能ポイント
- **セクション (`{{#key}}`)**: 配列の反復処理。`{{#key}}...{{else}}...{{/key}}` で空の場合の代替を描画。
- **条件分岐 (`{{#if}} / {{else}}`)**: データの有無や状態による表示の切り替え。
- **特殊変数 (`loop.index`, `loop.is_first`, `loop.is_last`)**: ループ内の位置に応じたスタイリング。

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
      <li class="{{#if loop.is_first}}first-item{{/if}}">
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
- **`loop.index`**: 行番号の表示。

### コード例

```cpp
auto tmpl = R"(
ID   | Name                 | Status
-----|----------------------|-------
{{#users}}
{{loop.index | zerofill(4)}} | {{name | right(20)}} | {{status | center(7)}}
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
('{{{name}}}', {{age}}, '{{{address.city}}}'){{#if loop.is_last}};{{else}},{{/if}}
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

## 6. injamm と std::format / fmt の使い分け

ループなしの単純な変数置換テンプレートに限定した場合の速度比較です。以下は `examples/bench_format.cpp`（`injamm_bench_format`）による実測値です（`docs/bench_format_2026-08-30.md`、GCC 16.2.1 Release `-O3 -DNDEBUG -march=native`、Ryzen 7 1700、200,000 回/ケースを 7 回実行した中央値。値は CPU・コンパイラにより変動します）。

> **NTTP のコンパイル時アンロール**：`render<fixed_string>`（NTTP）は単純テンプレートを `ct_exec.hpp` のコンパイル時アンロール実行器で処理します（`ct_is_unrollable` 判定、非該当はランタイム VM にフォールバック）。実行時ディスパッチループを除去し、フィールドアクセスを直接アクセスに特化するため、fmt の `FMT_COMPILE` に近い性能が出ます。`engine<T>`（実行時コンパイル）は従来どおり VM で実行します。

### 6.1 std::format との比較

`std::format` は毎回フォーマット文字列を実行時パースするため、変数が多いほど injamm（NTTP アンロール）が有利になります。1 変数 string のみ `std::format` が約 1.4 倍高速で、それ以外は injamm が 1.6–4.5 倍高速です（生出力 `{{{var}}}` で比較すると 3.65 倍）。

| ケース | std::format | injamm | 比率 (format/injamm) |
| :--- | ---: | ---: | ---: |
| 1 変数 (string) | 42.7 ns | 59.3 ns | 0.71x |
| 1 変数 (int) | 40.5 ns | 25.2 ns | 1.60x |
| 1 変数 (double) | 138.2 ns | 33.2 ns | 4.16x |
| 2 変数 (string/int) | 140.8 ns | 46.5 ns | 3.03x |
| 2 変数 (string/double) | 213.6 ns | 55.9 ns | 3.82x |
| 2 変数 (int/double) | 237.0 ns | 63.4 ns | 3.73x |
| 3 変数 (string/int/double) | 277.8 ns | 73.0 ns | 3.78x |
| 10 変数 (5 string + 3 int + 2 double) | 873.6 ns | 194.2 ns | 4.48x |
| 3 変数・エスケープあり `{{...}}` | 270.0 ns | 185.1 ns | 1.46x |
| 3 変数・生出力 `{{{...}}}` | 270.0 ns | 73.9 ns | 3.65x |
| 3 変数・runtime `engine<T>`（構築済み） | 276.8 ns | 157.9 ns | 1.73x |
| 3 変数・バッファ再利用 NTTP `render(d, out)` | 293.1 ns (format_to) | 73.3 ns | 4.02x |
| 3 変数・バッファ再利用 engine `render(d, out)` | 293.1 ns (format_to) | 136.9 ns | 2.15x |

### 6.2 fmt::format（FMT_COMPILE）との比較

`fmt::format(FMT_COMPILE("..."), ...)` はフォーマット文字列を**コンパイル時パース**してフォーマット処理を引数型ごとに完全特化します。injamm の NTTP アンロール実行も同じ戦略のため、**2 変数以上では injamm が同等以上**になります。1 変数の `{{var}}`（エスケープ付き）のみ fmt が約 4 倍高速です（fmt はエスケープを行わないため）。

| ケース | fmt (FMT_COMPILE) | injamm (NTTP) | 比率 (fmt/injamm) |
| :--- | ---: | ---: | ---: |
| 1 変数 (string) | 13.4 ns | 59.3 ns | 0.23x |
| 2 変数 (string/int) | 61.8 ns | 46.5 ns | 1.33x |
| 3 変数 (string/int/double) | 91.9 ns | 73.0 ns | 1.28x |
| 10 変数 (5 string + 3 int + 2 double) | 195.4 ns | 194.2 ns | 1.00x |

### 使い分けの指針

**`std::format` / `fmt::format` を使う場面**
- コードに直書きした静的なフォーマット（ログ、エラーメッセージ、1 変数程度の出力）
- 幅・精度・埋め・hex などのフォーマット指定子が必要
- HTML エスケープが不要
- 1 変数の単純置換で最速を狙う → `fmt::format(FMT_COMPILE(...))`（`{{var}}` で約 4 倍、`{{{var}}}` 生出力比較では fmt 13.4 ns vs injamm 73.9 ns 相当）。fmt が使えない環境では `std::format`（1 変数 string のみ 0.71x で injamm より速い）

**`injamm` を使う場面**
- テンプレート文字列が実行時に決まる（設定ファイル・ユーザー入力）→ `engine<T>`
- HTML 自動エスケープが必要（`{{var}}`）
- Mustache 構文（ループ・if/else・フィルタ）を扱いたい
- glaze リフレクションで定義した構造体データをそのまま描画したい
- NTTP の単純変数置換では 2 変数以上で `fmt::format(FMT_COMPILE)` と同等以上に高速（2 vars 1.33x, 3 vars 1.28x, 10 vars 1.00x）

**注意点**
- `{{var}}` は HTML エスケープを行うため、`std::format` / `fmt` と出力が異なります。生出力には `{{{var}}}` を使います（3 vars では 185.1 ns → 73.9 ns に短縮、ratio 1.46x→3.65x）。
- NTTP のアンロール高速化は単純テンプレート（変数置換のみ）に限定されます。セクション・ループ・if を含むテンプレートはランタイム VM にフォールバックするため、`engine<T>` と同等の性能です（hybrid 437 ns vs VM 447 ns）。
- `engine<T>` は構築時にテンプレートをコンパイルしますが、構築は 1 回きりなので、同じテンプレートを複数回レンダリングするなら NTTP と同等の性能になります（3 vars fresh: NTTP 73.0 ns vs engine 157.9 ns、再利用で差は縮小）。
- バッファ再利用（`render(d, out)`）を使うと高速化でき、`std::format_to` より有利です（NTTP 4.02x, engine 2.15x）。詳細は `docs/bench_format_2026-08-30.md` 参照。

---

## 7. 付録：フィルタ・特殊変数リファレンス

### 特殊変数
| 変数名 | 説明 |
| :--- | :--- |
| `{{loop.index}}` | ループの現在のインデックス (0始まり) |
| `{{loop.is_first}}` | ループの最初の要素であれば `true` |
| `{{loop.is_last}}` | ループの最後の要素であれば `true` |
| `{{loop.key}}` | Mapの反復時に現在のキー、または配列のインデックス |
| `{{root}}` | ルートオブジェクト全体のシリアライズ |
| `{{this}}` | 現在のコンテキストオブジェクト自体のシリアライズ |
| `{{#break}}` | ループを途中で終了 |
| `{{#continue}}` | 現在の要素をスキップして次へ |

### 主なフィルタ
- **文字列**: `upper`, `lower`, `trim`/`strip`（Python 空白セット、`strip("xy")` で文字集合指定可）, `truncate(n)`, `left(n)`, `right(n)`, `center(n)`, `substr(start, len)`
- **整数**: `abs`, `hex`, `oct`, `bin`, `numify` (カンマ区切り), `zerofill(n)`, `mod(n)`, `eq(n)`
- **実数**: `precision(n)`
