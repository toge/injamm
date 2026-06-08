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
#include "injamm/injamm.hpp"
#include "injamm/escape_hatch.hpp"

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
#include "injamm/injamm.hpp"
#include "injamm/escape_hatch.hpp"
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

## 注意事項

- **NTTP コンパイル時レンダリング** (`render<fixed_string>`) はセクション・if/else に対応していない（既知の制限）。複雑なテンプレートにはバイトコード VM を使用すること。
- `render<fixed_string>` の戻り値型 `expected<std::string>` は、GCC 16 の `[[nodiscard]] expected<void, error_ctx>` と衝突する可能性がある。必要に応じて `void` 特殊化を無視する。
- GCC 以外のコンパイラでは `INJAMM_ENABLE_THREADED_DISPATCH` を OFF にすること。
- コンテキスト型への glaze メタプログラミング（`glz::meta<T>` の特殊化）が必須。
