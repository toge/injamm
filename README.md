# injamm - inja minus minus

Mustache/inja サブセットの高速テンプレートエンジン。
Glaze でメタプログラミングされた C++ 構造体をコンテキストとして、テンプレートをレンダリングします。

2つのレンダリング API を提供:
- **NTTP コンパイル時** (`render<fixed_string>`): テンプレート文字列がコンパイル時定数の場合に最適
- **バイトコード VM** (`engine<T>`): テンプレート文字列が実行時まで決まらない場合に使用

## 特徴

- **ヘッダオンリー**: インクルードするだけで使用可能
- **高速**: コンパイル時テンプレートパース、Computed goto ディスパッチ（GCC）、Glaze リフレクションによる O(1) フィールドアクセス
- **依存最小**: Glaze のみ必須

## 要件

- C++23 対応コンパイラ（GCC 14+ 推奨）
- [Glaze](https://github.com/stephenberry/glaze)

## ビルド・インストール

```bash
# vcpkg を使う場合
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=~/vm/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build
ctest --test-dir build -V

# インストール
cmake --install build --prefix /usr/local

# システムインストール済み glaze を使う場合
cmake -B build -S .
cmake --build build
```

### CMake オプション

| オプション                 | 既定値 | 説明                                                     |
| -------------------------- | ------ | -------------------------------------------------------- |
| `ENABLE_THREADED_DISPATCH` | ON     | 高速化のためのGCC computed gotoディスパッチ（GCC のみ） |
| `BUILD_TEST`               | ON     | テストをビルドする                                       |
| `BUILD_EXAMPLE`            | ON     | サンプルをビルドする                                     |

### find_package

```cmake
find_package(injamm CONFIG REQUIRED)
target_link_libraries(myapp PRIVATE injamm::injamm)
```

## 使い方

より詳細なユースケースや具体的な活用例については、[ユースケースガイド](docs/USE_CASES.md) を参照してください。

### 1. データ型を定義する

```cpp
struct User {
  std::string name;
  int age{};
};

// メタデータの定義はなくてもよいです
template <>
struct glz::meta<User> {
  static constexpr auto value = glz::object("name", &User::name, "age", &User::age);
};
```

### 2. API の選択基準

**NTTP（`render<fixed_string>`）を選ぶ場合:**
- テンプレート文字列がコンパイル時定数（リテラル）である
- 特に理由がなければ常にこちらを推奨
- パースエラーはコンパイル時に検出される
- 2回目以降のレンダリングにアロケーションオーバーヘッドがない
- すべてのテンプレート構文（セクション、if/else、@変数、フィルター）に対応

**Bytecode VM（`engine<T>`）を選ぶ場合:**
- テンプレート文字列が実行時まで決まらない（ファイル読み込み、ユーザー入力など）
- 1回だけのレンダリングでも compile 済み engine を保持すれば NTTP と同等の性能

要するに: **テンプレートが固定なら NTTP、動的なら engine。**

### 3. NTTP コンパイル時レンダリング（推奨）

`fixed_string` と `render<kTmpl>(value)` でコンパイル時にテンプレートをパースします。
テンプレート文字列が固定（コンパイル時定数）の場合は常にこちらを使用してください。

```cpp
constexpr auto kTmpl = injamm::fixed_string("Hello {{name}}! You are {{age}}.");
auto r = injamm::render<kTmpl>(User{"Bob", 25});
// r == "Hello Bob! You are 25."
```

### 4. バイトコード VM（テンプレートが動的な場合）

`engine<T>` にテンプレート文字列を渡し、`.render(value)` でレンダリングします。
テンプレート文字列が実行時まで決まらない場合のみ使用してください。

```cpp
#include "injamm.hpp"

auto bc = injamm::engine<User>("{{name}} ({{age}})");
auto r = bc.render(User{"Alice", 30});
// r == "Alice (30)"
```

### 5. @var 定数置換（runtime / NTTP）

`@var(name)` はテンプレート構文としてではなく、プリプロセッサマクロのようにパース前に展開されます。

**Runtime（engine）**:
```cpp
// 通常の engine 構築時に定数マップを渡す
std::map<std::string, std::string, std::less<>> consts{
  {"f", "name"},
  {"s", "items"}
};
auto bc = injamm::engine<Data>("{{#@var(s)}}{{@var(f)}}{{/@var(s)}}", consts);
// → 内部的に "{{#items}}{{name}}{{/items}}" としてコンパイルされる
auto r = bc.render(Data{{{"Alice"}, {"Bob"}}});
// r == "AliceBob"
```

**NTTP（render）**:
```cpp
// テンプレート引数で key, value を交互に渡す
auto r = injamm::render<"{{@var(f)}} | upper", "f", "name">(User{"alice", 25});
// → コンパイル時に "{{name | upper}}" に展開
// r == "ALICE"
```

### 完全な例

```cpp
#include "injamm.hpp"
#include <glaze/glaze.hpp>
#include <iostream>
#include <vector>

struct User { std::string name; int age{}; };
template <> struct glz::meta<User> {
  static constexpr auto value = glz::object("name", &User::name, "age", &User::age);
};

struct Data { std::vector<User> users; };
template <> struct glz::meta<Data> {
  static constexpr auto value = glz::object("users", &Data::users);
};

int main() {
  Data data{{{"Alice", 30}, {"Bob", 25}, {"Charlie", 35}}};

  // バイトコード VM
  auto bc = injamm::engine<Data>(
    "{{#users}}{{name}} ({{age}})"
    "{{#if @last}}.{{else}}, {{/if}}{{/users}}");
  std::cout << *bc.render(data) << "\n";
  // "Alice (30), Bob (25), Charlie (35)."

  // NTTP コンパイル時レンダリング
  constexpr auto kTmpl = injamm::fixed_string("Hello {{name}}! Age: {{age}}.");
  std::cout << *injamm::render<kTmpl>(User{"Bob", 25}) << "\n";
  // "Hello Bob! Age: 25."
}
```

## テンプレート構文

| 構文                                | 説明                                  |
| ----------------------------------- | ------------------------------------- |
| `{{var}}`                           | 変数（HTML エスケープあり）           |
| `{{{var}}}`                         | 変数（HTML エスケープなし）           |
| `{{#section}}...{{/section}}`       | セクション（配列ループまたは真理値）  |
| `{{^section}}...{{/section}}`       | 逆セクション（空/偽 のとき描画）      |
| `{{#break}}`                        | ループを中断する                      |
| `{{#continue}}`                     | ループをスキップして次の反復へ        |
| `{{#if cond}}...{{/if}}`            | 条件分岐（0/空/偽は偽、それ以外は真） |
| `{{#if cond}}...{{else}}...{{/if}}` | if/else                               |
| `{{@index}}`                        | ループインデックス（0 始まり）        |
| `{{@first}}`                        | 最初の要素なら `true`                 |
| `{{@last}}`                         | 最後の要素なら `true`                 |
| `{{foo.bar.baz}}`                   | ネストパス                            |
| `{{@var(name)}}`                   | 定数置換（engine 構築時に渡した定数テーブルで展開、NTTP ではテンプレート引数で指定） |

### @var 定数置換

`{{@var(name)}}` はテンプレートをパースする**前**にプリプロセッサ的に展開されます。テンプレート構文を適用する前に定数で置換されるため、フィールド名のエイリアスやセクションキーのパラメータ化に使用できます。

値に `@var(yyy)` が含まれている場合は再帰的に展開します。

## フィルター

`|` 記法で変数の出力前に文字列・数値の変換を適用できます。engine 版と NTTP 版の両方でサポート。

### 文字列フィルター

| フィルタ      | 説明                                                | 構文例                    |
| ------------- | --------------------------------------------------- | ------------------------- |
| `upper`       | ASCII 小文字→大文字                                 | `{{name \| upper}}`       |
| `lower`       | ASCII 大文字→小文字                                 | `{{name \| lower}}`       |
| `capitalize`  | 先頭の文字を大文字                                  | `{{name \| capitalize}}`  |
| `title`       | 単語の先頭を大文字                                  | `{{name \| title}}`       |
| `trim`        | 先頭末尾の空白除去                                  | `{{name \| trim}}`        |
| `ltrim`       | 先頭の空白除去                                      | `{{name \| ltrim}}`       |
| `rtrim`       | 末尾の空白除去                                      | `{{name \| rtrim}}`       |
| `left(n)`     | n 文字分の枠をとり左寄せ                            | `{{name \| left(10)}}`    |
| `right(n)`    | n 文字分の枠をとり右寄せ                            | `{{name \| right(10)}}`   |
| `center(n)`   | n 文字分の枠をとり中央寄せ                          | `{{name \| center(10)}}`  |
| `truncate(n)` | n 文字以下ならそのまま、超えたら先頭 n-3 文字+"..." | `{{name \| truncate(8)}}` |
| `substr(n)`   | n 文字目から末尾まで                                | `{{name \| substr(2)}}`   |
| `substr(n,m)` | n 文字目から m 文字分                               | `{{name \| substr(1,3)}}` |

### 整数フィルター

| フィルタ | 説明                   | 構文例              |
| -------- | ---------------------- | ------------------- |
| `abs`    | 絶対値                 | `{{age \| abs}}`    |
| `neg`    | 符号の逆転             | `{{age \| neg}}`    |
| `hex`    | 16 進数表記            | `{{age \| hex}}`    |
| `oct`    | 8 進数表記             | `{{age \| oct}}`    |
| `bin`    | 2 進数表記             | `{{age \| bin}}`    |
| `mod(n)` | n で割った余り         | `{{age \| mod(5)}}` |
| `numify` | 3 桁ごとにカンマ区切り | `{{age \| numify}}` |

### フィルターのチェーン

複数のフィルターを組み合わせ可能。左から右に順に適用されます。

```cpp
{{name | trim | upper}}        // "  hello  " → "HELLO"
{{name | left(10) | upper}}    // "hi" → "HI       "
{{age | abs | numify}}         // -1234567 → "1,234,567"
```

## API リファレンス

### `injamm::fixed_string<N>`

コンパイル時文字列定数。NTTP としてテンプレート文字列を渡すために使用します。

```cpp
constexpr auto kTmpl = injamm::fixed_string("Hello {{name}}!");
```

### `injamm::render<kTmpl>(value)`
### `injamm::render<kTmpl, keys...>(value)`

NTTP ベースのコンパイル時レンダリング。戻り値は `expected<std::string>`。
第2テンプレート引数以降に `"key1", "val1", "key2", "val2"` のように key-value ペアを渡すと、`@var(name)` がコンパイル時に展開されます。

```cpp
auto r = injamm::render<kTmpl>(data);
if (r) { /* 成功: *r */ }
else   { /* 失敗: r.error().ec, r.error().position */ }

// @var 定数置換付き
auto r2 = injamm::render<"{{@var(f)}} | upper", "f", "name">(user);
```

### `injamm::engine<T>(tmpl_str)`
### `injamm::engine<T>(tmpl_str, consts_map)`

バイトコード VM。テンプレート文字列を実行時にコンパイルし、同じ型に対して複数回レンダリングする場合に効率的。
第2引数で `@var(name)` 展開用の定数マップを渡せます。任意の map-like コンテナ（`find()` + `->second` が `string_view` に変換可能）に対応。

```cpp
auto bc = injamm::engine<Data>("{{name}}");
auto r1 = bc.render(data1);
auto r2 = bc.render(data2);

// @var 定数置換付き
std::map<std::string, std::string, std::less<>> c{{"f", "name"}};
auto bc2 = injamm::engine<User>("{{@var(f)}}", c);
```

### `injamm::expected<T>`

`std::expected<T, error_ctx>` のエイリアス。

### `injamm::error_code`

| 値  | 名前           | 意味                       |
| --- | -------------- | -------------------------- |
| 0   | none           | エラーなし                 |
| 1   | no_read_input  | 入力なし                   |
| 2   | unexpected_end | 予期しないテンプレート終端 |
| 3   | unknown_key    | 不明なキー                 |
| 4   | syntax_error   | 構文エラー                 |
| 5   | type_mismatch  | 型不一致                   |
| 6   | invalid_utf8   | 不正な UTF-8               |
| 7   | unknown_filter | 不明なフィルタ             |

## 注意事項

- `render<fixed_string>` の戻り値型 `expected<std::string>` は、GCC 16 の `[[nodiscard]] expected<void, error_ctx>` と衝突する可能性があります。必要に応じて `void` 特殊化を無視してください。
- GCC 以外のコンパイラでは `ENABLE_THREADED_DISPATCH` を OFF にしてください。
