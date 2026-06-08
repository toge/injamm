# injamm - inja minus minus

injaサブセットの高速化を重視したテンプレートエンジンです。

glaze でメタプログラミングされた C++ 構造体をコンテキストとして、Mustache / inja 風のテンプレートをレンダリングします。
現時点ではコンパイル時テンプレートパースとバイトコード VM の両方を提供しています。

## 特徴

- **2つの API**: バイトコード VM（実行時コンパイル、全機能対応）と NTTP コンパイル時レンダリング（簡易変数のみ）
- **高速**: コンパイル時テンプレートパース、 Computed goto ディスパッチ（GCC）、Glazeが提供するリフレクションによる O(1) フィールドアクセス
- **ヘッダオンリー**: インクルードするだけで使用可能
- **依存最小**: Glaze のみ必須

## 要件

- C++26 対応コンパイラ（GCC 16+ 推奨）
- [glaze](https://github.com/stephenberry/glaze)（反射ベースのシリアライゼーション）

## ビルド・インストール

```bash
# vcpkg を使う場合
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build
ctest --test-dir build -V

# インストール
cmake --install build --prefix /usr/local

# システムインストール済み glaze を使う場合
cmake -B build -S .
cmake --build build
```

### CMake オプション

| オプション | 既定値 | 説明 |
|---|---|---|
| `INJAMM_ENABLE_THREADED_DISPATCH` | ON | GCC computed goto による高速ディスパッチ（GCC 限定） |
| `INJAMM_BUILD_TESTS` | ON | テストをビルドする |
| `INJAMM_BUILD_EXAMPLES` | ON | サンプルをビルドする |

### find_package

```cmake
find_package(injamm CONFIG REQUIRED)
target_link_libraries(myapp PRIVATE injamm::injamm)
```

## 使い方

### 1. データ型を定義する

```cpp
struct User {
  std::string name;
  int age{};
};

template <>
struct glz::meta<User> {
  static constexpr auto value = glz::object("name", &User::name, "age", &User::age);
};
```

### 2. バイトコード VM（推奨）

`bc_template<T>` にテンプレート文字列を渡し、`.render(value)` でレンダリングする。セクション、if/else、@index/@first/@last、ネストパスのすべてに対応。

```cpp
#include "injamm.hpp"

auto bc = injamm::bc_template<User>("{{name}} ({{age}})");
auto r = bc.render(User{"Alice", 30});
// r == "Alice (30)"
```

### 3. NTTP コンパイル時レンダリング

`fixed_string` と `render<kTmpl>(value)` でコンパイル時にテンプレートをパースする。簡易変数のみ対応。

```cpp
constexpr auto kTmpl = injamm::fixed_string("Hello {{name}}! You are {{age}}.");
auto r = injamm::render<kTmpl>(User{"Bob", 25});
// r == "Hello Bob! You are 25."
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
  auto bc = injamm::bc_template<Data>(
    "{{#users}}{{name}} ({{age}})"
    "{{#if @last}}.{{else}}, {{/if}}{{/users}}");
  std::cout << *bc.render(data) << "\n";  // "Alice (30), Bob (25), Charlie (35)."

  // NTTP コンパイル時レンダリング
  constexpr auto kTmpl = injamm::fixed_string("Hello {{name}}! Age: {{age}}.");
  std::cout << *injamm::render<kTmpl>(User{"Bob", 25}) << "\n";
  // "Hello Bob! Age: 25."
}
```

## テンプレート構文

| 構文                                | 説明                                   |
| ----------------------------------- | -------------------------------------- |
| `{{var}}`                           | 変数（HTMLエスケープあり）             |
| `{{{var}}}`                         | 変数（HTMLエスケープなし）             |
| `{{#section}}...{{/section}}`       | セクション（配列のループまたは真理値） |
| `{{^section}}...{{/section}}`       | 逆セクション（空/偽 のとき描画）       |
| `{{#if cond}}...{{/if}}`            | 条件分岐（0/空/偽は偽、それ以外は真）  |
| `{{#if cond}}...{{else}}...{{/if}}` | if/else                                |
| `{{@index}}`                        | 現在のループインデックス（0始まり）    |
| `{{@first}}`                        | 最初の要素なら `true`                  |
| `{{@last}}`                         | 最後の要素なら `true`                  |
| `{{foo.bar.baz}}`                   | ネストパス                             |

## フィルター

`|` 記法で変数の出力前に文字列・数値の変換を適用できる。BC版とNTTP版の両方でサポート。

### 文字列フィルター

| フィルタ | 説明 | 構文例 |
|----------|------|--------|
| `upper` | ASCII小文字→大文字 | `{{name \| upper}}` |
| `lower` | ASCII大文字→小文字 | `{{name \| lower}}` |
| `capitalize` | 先頭の文字を大文字 | `{{name \| capitalize}}` |
| `title` | 単語の先頭を大文字 | `{{name \| title}}` |
| `trim` | 先頭末尾の空白除去 | `{{name \| trim}}` |
| `ltrim` | 先頭の空白除去 | `{{name \| ltrim}}` |
| `rtrim` | 末尾の空白除去 | `{{name \| rtrim}}` |
| `left(n)` | n文字分の枠をとり左寄せ | `{{name \| left(10)}}` |
| `right(n)` | n文字分の枠をとり右寄せ | `{{name \| right(10)}}` |
| `center(n)` | n文字分の枠をとり中央寄せ | `{{name \| center(10)}}` |
| `truncate(n)` | n文字以下ならそのまま、超えたら先頭n-3文字+"..." | `{{name \| truncate(8)}}` |
| `substr(n)` | n文字目から末尾まで | `{{name \| substr(2)}}` |
| `substr(n,m)` | n文字目からm文字分 | `{{name \| substr(1,3)}}` |

### 整数フィルター

| フィルタ | 説明 | 構文例 |
|----------|------|--------|
| `abs` | 絶対値 | `{{age \| abs}}` |
| `neg` | 符号の逆転 | `{{age \| neg}}` |
| `hex` | 16進数表記 | `{{age \| hex}}` |
| `oct` | 8進数表記 | `{{age \| oct}}` |
| `bin` | 2進数表記 | `{{age \| bin}}` |
| `mod(n)` | nで割った余り | `{{age \| mod(5)}}` |
| `numify` | 3桁ごとにカンマ区切り | `{{age \| numify}}` |

### フィルターのチェーン

複数のフィルターを組み合わせ可能。左から右に順に適用される。

```cpp
{{name | trim | upper}}        // "  hello  " → "HELLO"
{{name | left(10) | upper}}    // "hi" → "HI       "
{{age | abs | numify}}         // -1234567 → "1,234,567"
```

## API リファレンス

### `injamm::fixed_string<N>`

コンパイル時文字列定数。NTTP としてテンプレート文字列を渡すために使用する。

```cpp
constexpr auto kTmpl = injamm::fixed_string("Hello {{name}}!");
```

### `injamm::render<kTmpl>(value)`

NTTP ベースのコンパイル時レンダリング。戻り値は `expected<std::string>`。

```cpp
auto r = injamm::render<kTmpl>(data);
if (r) { /* 成功: *r */ }
else   { /* 失敗: r.error().ec, r.error().position */ }
```

### `injamm::bc_template<T>(tmpl_str)`

バイトコード VM。テンプレート文字列を実行時にコンパイルし、同じ型に対して複数回レンダリングする場合に効率的。

```cpp
auto bc = injamm::bc_template<Data>("{{name}}");
auto r1 = bc.render(data1);
auto r2 = bc.render(data2);
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

- **NTTP コンパイル時レンダリング** (`render<fixed_string>`) はセクション・if/else に対応していない（既知の制限）。複雑なテンプレートにはバイトコード VM を使用すること。
- `render<fixed_string>` の戻り値型 `expected<std::string>` は、GCC 16 の `[[nodiscard]] expected<void, error_ctx>` と衝突する可能性がある。必要に応じて `void` 特殊化を無視する。
- GCC 以外のコンパイラでは `INJAMM_ENABLE_THREADED_DISPATCH` を OFF にすること。
- コンテキスト型への glaze メタプログラミング（`glz::meta<T>` の特殊化）が必須。
