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

ループなしの単純な変数置換テンプレートに限定した場合の速度比較です。以下は `examples/bench_format.cpp`（`injamm_bench_format`）による実測値です（`docs/bench_format_2026-08-30.md` は 7回中央値、`docs/bench_format_2026-08-31.md` は runtime 追加後の Release 単回計測（同条件 `-O3 -DNDEBUG -march=native`、GCC 16.2.1、Ryzen 7 1700）。値は CPU・コンパイラ・計測回数により変動します）。

> **NTTP専用アンロールが存在する**：`render<fixed_string>`（NTTP）は単純テンプレートを `include/injamm/ct_exec.hpp` の専用実行器（`ct_executor` / `ct_hybrid_executor`、`ct_is_unrollable` 判定）で **コンパイル時に全命令をアンロール** し、VM の dispatch ループを除去・フィールドアクセスを直接呼出しに特化します。非該当テンプレート（セクション/if/フィルタ含む）は VM にフォールバックします。`engine<T>`（実行時コンパイル）は常に `bytecode_exec` VM で実行し、このアンロールを経由しません。以下の **runtime (VM) 併記** が差を直接示します。

### 6.1 NTTP（ct unroll） vs std::format

`std::format` は毎回フォーマット文字列を実行時パースするため、変数が多いほど injamm NTTP が有利になります。`docs/bench_format_2026-08-31.md` の Release 単回でも NTTP は全ケースで `std::format` より高速です（7回中央値 `2026-08-30` では 1 var string のみ format が 0.71x で逆転したが、単回 Release では変動の範囲）。

| ケース | std::format | injamm NTTP (ct unroll) | 比率 (format/NTTP) |
| :--- | ---: | ---: | ---: |
| 1 変数 (string) `{{val}}` | 36.7 ns | 32.6 ns | 1.13x |
| 1 変数 (int) `{{val}}` | 40.7 ns | 26.2 ns | 1.56x |
| 1 変数 (double) `{{val}}` | 139.9 ns | 32.9 ns | 4.25x |
| 2 変数 (string/int) `test example, {{aaa}} = {{bbb}}` | 134.7 ns | 48.0 ns | 2.81x |
| 2 変数 (string/double) | 211.4 ns | 56.4 ns | 3.75x |
| 2 変数 (int/double) | 226.6 ns | 71.5 ns | 3.17x |
| 3 変数 (string/int/double) | 265.5 ns | 77.3 ns | 3.44x |
| 10 変数 (5 string + 3 int + 2 double) | 843.4 ns | 172.4 ns | 4.89x |
| 3 変数・エスケープあり `{{aaa}}` (HTML chars) | 264.7 ns | 170.5 ns | 1.55x |
| 3 変数・生出力 `{{{aaa}}}` | 264.7 ns | 73.1 ns | 3.62x |
| 3 変数・バッファ再利用 `render(d, out)` vs `format_to` | 291.4 ns (format_to) | 70.0 ns | 4.16x |

> 参考（7回中央値 `2026-08-30` の NTTP vs format）：1 var str 0.71x / 1 var int 1.60x / 1 var dbl 4.16x / 2 vars str/int 3.03x / 3 vars 3.78x / 10 vars 4.48x。詳細は `docs/bench_format_2026-08-30.md` 参照。

### 6.2 runtime engine（VM） vs std::format — runtime 版も計測

`engine<T>` はテンプレートを **構築時に1回だけ** コンパイルし、以降 `render()` は毎回 VM dispatch で実行します。NTTP の ct unroll を使わないため、NTTP より 1.5–3.2 倍遅いですが、依然として `std::format`（毎回パース）より 2 変数以上では高速です。1 変数 string/int のみ VM オーバーヘッドで `std::format` が僅かに速いケースがあります。

| ケース | std::format | injamm engine (VM, 構築済み) | 比率 (format/engine) |
| :--- | ---: | ---: | ---: |
| 1 変数 (string) | 36.7 ns | 62.5 ns | 0.59x (format faster) |
| 1 変数 (int) | 40.7 ns | 51.8 ns | 0.79x (format faster) |
| 1 変数 (double) | 139.9 ns | 80.5 ns | 1.74x |
| 2 変数 (string/int) | 134.7 ns | 111.9 ns | 1.20x |
| 2 変数 (string/double) | 211.4 ns | 125.5 ns | 1.68x |
| 2 変数 (int/double) | 226.6 ns | 125.5 ns | 1.81x |
| 3 変数 (string/int/double) fresh | 258.3 ns | 147.7 ns | 1.75x |
| 10 変数 (5str/3int/2dbl) | 843.4 ns | 549.8 ns | 1.53x |
| 3 変数・バッファ再利用 `render(d, out)` | 291.4 ns | 129.1 ns | 2.26x |

### 6.3 NTTP（ct unroll） vs engine（VM） — NTTP専用アンロールの効果

同一テンプレート・同一データで **NTTP のみ `ct_exec.hpp` アンロール** が効くため、この差が専用パスの効果です。直線テンプレートでは dispatch 除去で **1.5–3.2 倍** 高速化します。セクション/if を含むテンプレートは NTTP でも VM にフォールバックするため差が縮小します（hybrid 462 ns vs VM 472 ns、wide partial 407 ns vs 405 ns）。

| ケース | engine (VM) | NTTP (ct unroll) | 比率 (engine/NTTP) |
| :--- | ---: | ---: | ---: |
| 1 変数 (string) | 62.5 ns | 32.6 ns | **1.92x** NTTP faster |
| 1 変数 (int) | 51.8 ns | 26.2 ns | **1.98x** |
| 1 変数 (double) | 80.5 ns | 32.9 ns | **2.44x** |
| 2 変数 (string/int) | 111.9 ns | 48.0 ns | **2.33x** |
| 2 変数 (string/double) | 125.5 ns | 56.4 ns | **2.23x** |
| 2 変数 (int/double) | 125.5 ns | 71.5 ns | **1.75x** |
| 3 変数 (string/int/double) | 147.7 ns | 77.3 ns | **1.91x** |
| 10 変数 (5str/3int/2dbl) | 549.8 ns | 172.4 ns | **3.19x** |
| 3 変数・バッファ再利用 | 129.1 ns | 70.0 ns | **1.84x** |

### 6.4 fmt::format（FMT_COMPILE）との比較

`fmt::format(FMT_COMPILE("..."), ...)` はフォーマット文字列を**コンパイル時パース**して引数型ごとに完全特化します。injamm の NTTP アンロールも同じ戦略のため、**2 変数以上では NTTP が同等以上**。`engine (VM)` は fmt より遅くなります（VM dispatch のため）。

| ケース | fmt (FMT_COMPILE) | injamm NTTP (ct unroll) | 比率 (fmt/NTTP) | fmt vs engine (VM) | 比率 (fmt/engine) |
| :--- | ---: | ---: | ---: | ---: | ---: |
| 1 変数 (string) | 13.5 ns | 32.6 ns | 0.41x (fmt faster) | 13.5 ns vs 62.5 ns | 0.22x |
| 2 変数 (string/int) | 56.9 ns | 48.0 ns | **1.19x** | 56.9 ns vs 111.9 ns | 0.51x |
| 3 変数 (string/int/double) | 91.0 ns | 77.3 ns | **1.18x** | 91.0 ns vs 147.7 ns | 0.62x |
| 10 変数 (5 string + 3 int + 2 double) | 193.7 ns | 172.4 ns | **1.12x** | 193.7 ns vs 549.8 ns | 0.35x |

> 参考（7回中央値 `2026-08-30` の fmt vs NTTP）：1 var 0.23x / 2 vars 1.33x / 3 vars 1.28x / 10 vars 1.00x。

### 使い分けの指針

**`std::format` / `fmt::format` を使う場面**
- コードに直書きした静的なフォーマット（ログ、エラーメッセージ、1 変数程度の出力）
- 幅・精度・埋め・hex などのフォーマット指定子が必要
- HTML エスケープが不要
- 1 変数の単純置換で最速を狙う → `fmt::format(FMT_COMPILE(...))`（1 var string で fmt 13.5 ns vs NTTP 32.6 ns、engine 62.5 ns）。fmt が使えない環境で 1 var string/int は `std::format` が engine より速いが NTTP には劣る（6.2 節）

**`injamm` を使う場面**
- テンプレート文字列が実行時に決まる（設定ファイル・ユーザー入力）→ `engine<T>`（VM、NTTP より 1.5–3.2 倍遅いが 2 変数以上で `std::format` より高速）
- HTML 自動エスケープが必要（`{{var}}`）
- Mustache 構文（ループ・if/else・フィルタ）を扱いたい
- glaze リフレクションで定義した構造体データをそのまま描画したい
- NTTP の単純変数置換では 2 変数以上で `fmt::format(FMT_COMPILE)` と同等以上に高速（2 vars 1.19x, 3 vars 1.18x, 10 vars 1.12x — §6.4）。**engine は fmt より遅い**（0.35–0.62x）ため、固定テンプレートで最速が必要なら NTTP を使う

**注意点**
- `{{var}}` は HTML エスケープを行うため、`std::format` / `fmt` と出力が異なります。生出力には `{{{var}}}` を使います（3 vars では 170.5 ns → 73.1 ns に短縮、ratio 1.55x→3.62x）。
- NTTP のアンロール高速化は単純テンプレート（変数置換のみ）に限定されます。セクション・ループ・if を含むテンプレートはランタイム VM にフォールバックするため、`engine<T>` と同等の性能です（hybrid 462 ns vs 472 ns、wide partial 407 ns vs 405 ns）。
- `engine<T>` は構築時にテンプレートをコンパイルします（1回きり）。直線テンプレートでは VM dispatch のため NTTP より 1.5–3.2 倍遅く（§6.3）、10 vars で差が最大（549.8 ns vs 172.4 ns、3.19x）。`NTTP専用アンロール` の有無がこの差です。
- バッファ再利用（`render(d, out)`）を使うと高速化でき、`std::format_to` より有利です（NTTP 4.16x, engine 2.26x）。詳細は `docs/bench_format_2026-08-31.md`（runtime 追加）および `docs/bench_format_2026-08-30.md` 参照。

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
