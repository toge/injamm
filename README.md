# injamm - inja minus minus

Mustache/inja サブセットの高速テンプレートエンジン。
Glaze でメタプログラミングされた C++ 構造体をコンテキストとして、テンプレートをレンダリングします。

3つのレンダリング API を提供:
- **NTTP コンパイル時** (`render<fixed_string>`): テンプレート文字列がコンパイル時定数の場合に最適
- **バイトコード VM** (`engine<T>`): テンプレート文字列が実行時まで決まらない場合に使用
- **AOT コード生成** (`injamm_codegen`): バイトコード/テンプレートから glaze 非依存の C++ レンダリング関数を生成。VM より高速な直接フィールドアクセス

## 特徴

- **ヘッダオンリー**: インクルードするだけで使用可能
- **高速**: コンパイル時テンプレートパース、Computed goto ディスパッチ（GCC）、Glaze リフレクションによる O(1) フィールドアクセス、AOT コード生成（`injamm_codegen`）による glaze 非依存の直接アクセス
- **依存最小**: [Glaze](https://github.com/stephenberry/glaze) のみ必須。enum 名前解決は [enchantum](https://github.com/anomalyco/enchantum)（オプション、`ENABLE_ENUM` で切替）

## 注意: Mustache/inja との挙動の違い

injamm は Mustache/inja サブセットですが、以下の点で挙動が異なります。

- **存在しないキーはエラー**: `{{missing}}` のような未定義キーは空文字ではなく
  `unknown_key` エラーを返します（Mustache は空文字、inja は例外を送出）。
- **親スタック解決はコンパイル時**: セクション内の変数は Mustache と同様に
  現在の要素 → 内側のセクション → ルートの順で解決されますが、この解決は
  コンパイル時（バイトコード生成時）に行われ、実行時コストはゼロです。
  ルート参照の明示指定 `{{root.field}}` も引き続き使用できます
  （[SYNTAX.md](SYNTAX.md) 参照）。

テンプレート構文がどう処理されるのかは[こちら](https://toge.github.io/injamm-web-helper/)で確認できます。

## 要件

- C++23 対応コンパイラ（GCC 14+ 推奨）
- [Glaze](https://github.com/stephenberry/glaze)（高速浮動小数点解析も glaze 内蔵の fast_float を利用）
- [enchantum](https://github.com/anomalyco/enchantum)（C++20 enum→string 反射ライブラリ、オプション。`ENABLE_ENUM=OFF` で不要）

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
| `BUILD_UTIL`               | OFF    | CLI ユーティリティ（`injamm_bc` / `injamm_codegen`）をビルドする            |
| `ENABLE_ENUM`              | ON     | enchantum による enum 文字列出力を有効化（WASI minimal でも有効。`ENCHANTUM_THROW` を `trap` に差し替えて動作） |
| `ENABLE_WASI_MINIMAL`      | OFF    | WASI minimal 環境向けに例外を無効化（自動検出なし、明示指定のみ。`wasm32-unknown-unknown` (freestanding) は非対応、[WASI minimal 対応](#wasi-minimal-対応)参照） |

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

**AOT コード生成（`injamm_codegen`）を選ぶ場合:**
- テンプレートがデプロイ前に確定しており、最大スループットが欲しい
- glaze 依存を外した生成ヘッダを配布・埋め込みたい
- `injamm_bc` で `.bc` 化 → `injamm_codegen -i tmpl.bc -t MyData -o render.hpp` でヘッダ生成

要するに: **テンプレートが固定なら NTTP、動的なら engine、確定済みで最速を求めるなら codegen。**

### 3. テンプレート機能の使い分けガイドライン

injamm は多くの機能を提供していますが、適切な使い分けが重要です。

#### 3.1 変数参照の基本

| 構文 | 使用場面 | 例 |
|------|----------|-----|
| `{{var}}` | 基本的な変数出力（HTMLエスケープあり） | `{{name}}` |
| `{{{var}}}` / `{{&var}}` | 生の出力が必要な場合（HTMLエスケープなし） | `{{{html_content}}}` / `{{&html_content}}` |
| `{{this}}` / `{{.}}` | 現在コンテキストの値（マップ反復等） | `{{this}}` / `{{.}}` |
| `{{field.subfield}}` | ネストしたフィールドアクセス | `{{user.address.city}}` |
| `{{array.0}}` | 配列のインデックスアクセス | `{{items.0}}` |
| `{{root}}` / `{{root.field}}` | ルートコンテキストアクセス（ループ内から外側を参照。外側フィールド名の暗黙参照も可） | `{{root.title}}` / `{{title}}` |

#### 3.2 セクションと条件分岐

| 構文 | 使用場面 | 例 |
|------|----------|-----|
| `{{#section}}...{{/section}}` | 配列のループ、または真理値による条件描画 | `{{#users}}{{name}}{{/users}}` |
| `{{#section}}...{{else}}...{{/section}}` | 空/偽の場合のフォールバック | `{{#users}}{{name}}{{else}}no users{{/users}}` |
| `{{^section}}...{{/section}}` | 空/偽の場合のみ描画（逆セクション） | `{{^empty}}content{{/empty}}` |
| `{{#if cond}}...{{/if}}` | 条件分岐 | `{{#if isActive}}active{{/if}}` |
| `{{#if x == rhs}}...{{/if}}` | 比較演算子付き条件分岐 | `{{#if age >= 18}}adult{{/if}}` |

**推奨**: シンプルな条件分岐には `{{#if}}` を使用し、ループにはセクションを使用してください。

#### 3.3 @var 定数置換

`@var(name)` はテンプレート構文としてではなく、プリプロセッサマクロのようにパース前に展開されます。

| 使用場面 | 例 | 説明 |
|----------|-----|------|
| フィールド名のエイリアス | `{{@var(f)}}` where `f="name"` | 長いフィールド名を短くする |
| セクションキーのパラメータ化 | `{{#@var(s)}}{{@var(f)}}{{/@var(s)}}` | 動的なセクションキー |
| テンプレートの再利用 | `{{@var(header)}}` | 共通ヘッダーの挿入 |

**注意**: @var はコンパイル時（NTTP）または engine 構築時（runtime）に展開されるため、実行時に値を変更することはできません。

#### 3.4 フィルターの使用

フィルターは変数の出力前に文字列・数値の変換を適用します。

| カテゴリ | 使用場面 | 例 |
|----------|----------|-----|
| 文字列変換 | 大文字化、トリミング等 | `{{name \| upper}}` |
| 数値変換 | 進数変換、フォーマット | `{{age \| hex}}` |
| フォーマット | パディング、切り詰め | `{{name \| left(10)}}` |
| セクション加工 | 反復要素の絞り込み・並べ替え | `{{#items \| reverse \| take(3)}}` |

**推奨**: フィルターチェーンは最大 4 つまで。複雑な変換が必要な場合は、C++ 側で前処理することを検討してください。詳細は [SYNTAX.md](SYNTAX.md) および下記「フィルター」章を参照。

#### 3.5 Partial の使用

| 構文 | 使用場面 | 例 |
|------|----------|-----|
| `{{#partialdef name}}...{{/partialdef}}` | 名前付き partial を定義（engine<T> と NTTP render の両方で可） | 共通UI部品の定義 |
| `{{#partial name}}` | 定義済み partial を描画（両方で可） | サイドバーの描画 |
| `{{> partial}}` | 外部から注入した断片を展開（engine<T> はレジストリ、NTTP render は entry pair） | ヘッダー/フッターの挿入 |

**推奨**:
- 両方の API で `#partialdef` / `#partial` が使えます。`#partialdef` は同一テンプレート内で定義します。
- `{{> name}}` はテンプレート文字列の**外**から本文を持ってきます。engine<T> はコンストラクタのレジストリ経由（`make_partial<T>`）、NTTP render は entry pair 経由です（内部は同じ partial メカニズム）。
- 部分描画 API（`render(data, "name")` / `render_partial<tmpl>(data, "name")`）は engine<T> と NTTP の両方で利用でき、HTMX 等の部分更新に使用できます。
- 修飾子 `now`（定義と同時展開）/ `local`（その場のみ展開、名前検索不可）を `{{#partialdef name now}}` / `{{#partialdef name local}}` で指定できます（併用可）。詳細は [SYNTAX.md §19.8](SYNTAX.md) 参照。

#### 3.6 ループ変数

| 変数 | 使用場面 | 例 |
|------|----------|-----|
| `{{loop.index}}` | 0始まりのインデックス | `{{loop.index}}: {{name}}` |
| `{{loop.index1}}` | 1始まりのインデックス | `Item {{loop.index1}}` |
| `{{loop.size}}` | 総要素数 | `Total: {{loop.size}}` |
| `{{loop.is_first}}` | 最初の要素か | `{{#if loop.is_first}}<ul>{{/if}}` |
| `{{loop.is_last}}` | 最後の要素か | `{{name}}{{^loop.is_last}}, {{/loop.is_last}}` |
| `{{loop.key}}` | マップのキー | `{{loop.key}}: {{value}}` |

#### 3.7 高度な機能

| 機能 | 使用場面 | 注意点 |
|------|----------|--------|
| `{{#break}}` / `{{#continue}}` | ループ制御 | セクション内でのみ使用可能 |
| `{{loop.parent.*}}` | 親ループへのアクセス | ネストしたループでのみ使用可能 |
| 論理演算子（`&&`, `||`, `!`） | 複雑な条件式 | 複雑になりすぎる場合はC++側で処理を検討 |

### 4. パフォーマンスのベストプラクティス

1. **テンプレートサイズ**: NTTP ではテンプレートサイズが大きくなるとコンパイル時間が増加します。目安として1テンプレートあたり数KB程度が推奨です。
2. **フィルター**: フィルターは実行時コストがかかるため、必要最小限にしてください。
3. **バッファ再利用**: 複数回レンダリングする場合は、バッファ再利用API (`render(value, out)`) を使用してください。
4. **並列レンダリング**: スレッドセーフであるため、並列レンダリングでスループットを向上できます。

### 5. NTTP コンパイル時レンダリング（推奨）

`fixed_string` と `render<kTmpl>(value)` でコンパイル時にテンプレートをパースします。
テンプレート文字列が固定（コンパイル時定数）の場合は常にこちらを使用してください。

```cpp
constexpr auto kTmpl = injamm::fixed_string("Hello {{name}}! You are {{age}}.");
auto r = injamm::render<kTmpl>(User{"Bob", 25});
// r == "Hello Bob! You are 25."
```

### 6. バイトコード VM（テンプレートが動的な場合）

`engine<T>` にテンプレート文字列を渡し、`.render(value)` でレンダリングします。
テンプレート文字列が実行時まで決まらない場合のみ使用してください。

```cpp
#include "injamm.hpp"

auto bc = injamm::engine<User>("{{name}} ({{age}})");
auto r = bc.render(User{"Alice", 30});
// r == "Alice (30)"
```

### 7. @var 定数置換（runtime / NTTP）

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

### 8. 完全な例

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
    "{{#if loop.is_last}}.{{else}}, {{/if}}{{/users}}");
  std::cout << *bc.render(data) << "\n";
  // "Alice (30), Bob (25), Charlie (35)."

  // セクション with else（空/偽の場合に else 節を描画）
  Data empty_data{};
  auto bc2 = injamm::engine<Data>(
    "{{#users}}{{name}}{{else}}no users{{/users}}");
  std::cout << *bc2.render(empty_data) << "\n";
  // "no users"

  // NTTP コンパイル時レンダリング
  constexpr auto kTmpl = injamm::fixed_string("Hello {{name}}! Age: {{age}}.");
  std::cout << *injamm::render<kTmpl>(User{"Bob", 25}) << "\n";
  // "Hello Bob! Age: 25."
}
```

### 9. バイトコードの保存と読み込み

コンパイル済みのバイトコードをファイルに保存し、後で読み込んで再利用できます。
`field_index` は保存されず、読み込み時に glaze リフレクションで再解決されます。

WASI minimal 環境でもバイトコードの保存・読み込みは利用できます（例外のみ無効、ストリーム版・span 版の両方が有効）。

```cpp
#include "injamm.hpp"
#include <fstream>
#include <sstream>

// 保存
std::ostringstream oss;
auto ec = injamm::save_bytecode(engine.get_bytecode(), oss);
// ec == injamm::error_code::none なら成功

// 読み込み（テンプレートコンテキストと同じ型 T を指定）
std::istringstream iss(oss.str());
auto bc = injamm::load_bytecode<User>(iss);
// bc → expected<bytecode>
// bc.has_value() で成功判定、bc-> で利用

// 読み込んだバイトコードから engine を構築
auto eng = injamm::engine<User>(std::move(*bc));
auto r = eng.render(User{"Alice", 30});
```

**フォーマット**: カスタムバイナリ形式。マジックバイト `IJBC` + バージョン 1（リトルエンディアン）。

### 10. CLI ツール（`injamm_bc` / `injamm_codegen`）

`BUILD_UTIL=ON` でビルドされるコマンドラインツール。

#### injamm_bc — テンプレート → バイトコード

テンプレートファイルをコンパイルしてバイトコードファイル（`.bc`）を出力します。

```bash
# テンプレートファイルからバイトコード生成
injamm_bc -i template.mustache -o template.bc

# 標準入力から
echo "Hello {{name}}" | injamm_bc -i - -o hello.bc

# インライン文字列から
injamm_bc -e "{{#users}}{{name}}, {{/users}}" -o users.bc

# @var 定数置換付き
injamm_bc -i page.html -o page.bc -D title="My Page" -D footer="© 2026"
```

| オプション | 説明 |
|-----------|------|
| `-i, --input <path>` | テンプレートファイル（`-` で標準入力） |
| `-e, --expr <string>` | インラインテンプレート文字列 |
| `-o, --output <path>` | 出力バイトコードファイル（必須） |
| `-D, --define <k>=<v>` | `@var` 定数を定義（複数指定可） |
| `-h, --help` | ヘルプ表示 |

#### injamm_codegen — バイトコード/テンプレート → C++ ヘッダ（AOT）

`injamm_bc` の出力（`.bc`）またはテンプレート文字列から、glaze 非依存の C++ レンダリング関数を生成します。生成された関数は VM を介さず直接フィールドアクセスするため高速です。

```bash
# .bc から生成
injamm_codegen -i template.bc -t MyData -o render.hpp

# テンプレート文字列から直接生成
injamm_codegen -e "Hello {{name}}! {{#items}}{{this}} {{/items}}" -t MyData -o render.hpp
```

```cpp
#include "render.hpp"
// 生成された関数を利用（シグネチャはテンプレートにより異なる）
std::string out;
render_MyData(data, out);
```

| オプション | 説明 |
|-----------|------|
| `-i, --input <path>` | 入力バイトコードファイル（`.bc`） |
| `-e, --expr <string>` | インラインテンプレート文字列（`-i` の代替） |
| `-t, --type <Type>` | コンテキスト型名（必須） |
| `-o, --output <path>` | 出力ヘッダファイル（必須） |
| `-h, --help` | ヘルプ表示 |

対応機能: 変数展開（`{{var}}` / `{{{var}}}`）、HTML エスケープ、セクション、条件分岐（`{{#if}}`）、比較演算、フィルタ、ループ変数（`loop.*`）。詳細とパリティは `util/injamm_codegen.cpp` および `test_codegen/` を参照。

## テンプレート構文

全ての構文の詳細とサンプルコードは **[SYNTAX.md](SYNTAX.md)** を参照してください。以下は構文の一覧です。

| 構文                                | 説明                                  |
| ----------------------------------- | ------------------------------------- |
| `{{var}}`                           | 変数（HTML エスケープあり）           |
| `{{{var}}}`                         | 変数（HTML エスケープなし）           |
| `{{#section}}...{{/section}}`       | セクション（配列ループまたは真理値）  |
| `{{#section}}...{{else}}...{{/section}}` | セクション with else（空/偽の場合に else 節を描画） |
| `{{^section}}...{{/section}}`       | 逆セクション（空/偽 のとき描画）      |
| `{{^section}}...{{else}}...{{/section}}` | 逆セクション with else（真の場合に else 節を描画） |
| `{{#exists var}}...{{/exists}}`     | 変数が存在（真とみなせる）ときに描画  |
| `{{^exists var}}...{{/exists}}`     | 変数が存在しない（偽/空）ときに描画  |
| `{{#partialdef name}}...{{/partialdef}}` | 名前付き partial の定義（engine<T> と NTTP render の両方で可） |
| `{{#partial name}}`               | 定義済み partial を現在のコンテキストで描画（両方で可） |
| `{{#break}}`                        | ループを中断する                      |
| `{{#continue}}`                     | ループをスキップして次の反復へ        |
| `{{#if cond}}...{{/if}}`            | 条件分岐（0/空/偽は偽、それ以外は真） |
| `{{#if cond}}...{{else}}...{{/if}}` | if/else                               |
| `{{#if x == rhs}}...{{/if}}`        | 直接比較（`==` / `!=` / `<` / `<=` / `>` / `>=`、右辺は整数リテラル・文字列リテラル・変数）。文字列リテラル比較は enum フィールドの列挙子名とも照合可能 |
| `{{#if a || b}}...{{/if}}`          | 単純な論理演算（`||` / `&&` / `!`） |
| `{{loop.index}}`                    | ループインデックス（0 始まり、inja 互換）|
| `{{loop.parent.index}}`             | 親ループの loop 変数へアクセス |
| `{{loop.index1}}`                   | ループインデックス（1 始まり、inja 互換）|
| `{{loop.size}}`                     | ループ総要素数（inja 互換）            |
| `{{loop.is_first}}`                 | 最初の要素なら `true`（inja 互換）     |
| `{{loop.is_last}}`                  | 最後の要素なら `true`（inja 互換）     |
| `{{loop.is_even}}` / `{{loop.is_odd}}` | 偶数/奇数インデックスなら `true`（zebra ストライプ用） |
| `{{loop.key}}`                      | マップ反復時のキー名 |
| `{{foo.bar.baz}}`                   | ネストパス                            |
| `{{items.0}}` / `{{items.0.name}}`  | 配列インデックスアクセス |
| `{{field.size}}`                   | コンテナの要素数                      |
| `{{this}}` / `{{.}}`                | 現在コンテキストの値（`{{.}}` は `{{this}}` の別名） |
| `{{&var}}`                          | 生出力（`{{{var}}}` と同等、Mustache 互換） |
| `{{root}}` / `{{root.field}}`       | ルートコンテキスト / フィールドへのアクセス |
| `{{! ... }}`                        | コメント（Mustache 標準構文）         |
| `{# ... #}`                         | コメント（injamm 拡張） |
| `{{~ var ~}}`                       | タグ前後の空白をトリム                |
| `{{@var(name)}}`                   | 定数置換（engine 構築時に渡した定数テーブルで展開、NTTP ではテンプレート引数で指定） |
| `{{> partial}}`                    | partial 展開（engine は `make_partial` / `{{> name}}`、NTTP は `ct_partials` / `{{> name}}`） |
| `{{#items \| reverse}}` / `take(n)` / `skip(n)` / `stride(n,m)` / `join("sep")` | セクションフィルタ（パイプライン） |

### @var 定数置換

`{{@var(name)}}` はテンプレートをパースする**前**にプリプロセッサ的に展開されます。テンプレート構文を適用する前に定数で置換されるため、フィールド名のエイリアスやセクションキーのパラメータ化に使用できます。

値に `@var(yyy)` が含まれている場合は再帰的に展開します。

### enum 型のサポート

C++ の `enum` / `enum class` 型は、Glaze リフレクションで構造体フィールドとして登録すると、テンプレート内で以下のように扱えます。

| 機能       | 例                            | 説明                                                                           |
| ---------- | ----------------------------- | ------------------------------------------------------------------------------ |
| 値の出力   | `{{status}}` → `"Active"`     | 列挙子名を文字列として出力（`{{}}` なら HTML エスケープ、`{{{}}}` なら生出力） |
| 真偽判定   | `{{#if status}}`              | 非0の列挙値は真、0は偽                                                         |
| 文字列比較 | `{{#if status == "Pending"}}` | 列挙子名との等値/不等値比較（`==` / `!=`）が可能。NTTP ではコンパイル時に解決  |
| 未知値     | `{{unknown_enum}}` → `"42"`   | enchantum が認識しない値は underlying 整数を10進数で出力                       |

```cpp
enum class Status : int { Unknown = 0, Active = 1, Pending = 2 };

struct Task {
  std::string title;
  Status status{Status::Active};
};

template <> struct glz::meta<Task> {
  static constexpr auto value = glz::object("title", &Task::title, "status", &Task::status);
};

// enum 値の出力
auto bc = injamm::engine<Task>("{{title}}: {{status}}");
auto r = bc.render(Task{"write docs", Status::Pending});
// r == "write docs: Pending"

// enum 比較
auto bc2 = injamm::engine<Task>("{{#if status == \"Active\"}}active{{else}}inactive{{/if}}");
auto r2 = bc2.render(Task{"review", Status::Pending});
// r2 == "pending"

// NTTP でも同じ構文が使用可能
auto r3 = injamm::render<"{{title}}: {{status}}">(Task{"fix bug", Status::Active});
// r3 == "fix bug: Active"
```

## フィルター

`|` 記法で変数の出力前に文字列・数値の変換を適用できます。engine 版と NTTP 版の両方でサポート。

### 文字列フィルター

| フィルタ      | 説明                                                | 構文例                    |
| ------------- | --------------------------------------------------- | ------------------------- |
| `upper`       | ASCII 小文字→大文字                                 | `{{name \| upper}}`       |
| `lower`       | ASCII 大文字→小文字                                 | `{{name \| lower}}`       |
| `capitalize`  | 先頭の文字を大文字                                  | `{{name \| capitalize}}`  |
| `title`       | 単語の先頭を大文字                                  | `{{name \| title}}`       |
| `trim`        | 先頭末尾の空白除去（Python 空白セット）           | `{{name \| trim}}`        |
| `ltrim`       | 先頭の空白除去（Python 空白セット）               | `{{name \| ltrim}}`       |
| `rtrim`       | 末尾の空白除去（Python 空白セット）               | `{{name \| rtrim}}`       |
| `strip`       | trim の別名（先頭末尾の空白除去）                 | `{{name \| strip}}`       |
| `lstrip`      | ltrim の別名（先頭の空白除去）                    | `{{name \| lstrip}}`      |
| `rstrip`      | rtrim の別名（末尾の空白除去）                    | `{{name \| rstrip}}`      |
| `strip(chars)`| 指定した文字集合を先頭末尾から除去                | `{{name \| strip("xy")}}` |
| `left(n)`     | n 文字分の枠をとり左寄せ                            | `{{name \| left(10)}}`    |
| `right(n)`    | n 文字分の枠をとり右寄せ                            | `{{name \| right(10)}}`   |
| `center(n)`   | n 文字分の枠をとり中央寄せ                          | `{{name \| center(10)}}`  |
| `truncate(n)` | n 文字以下ならそのまま、超えたら先頭 n-3 文字+"..." | `{{name \| truncate(8)}}` |
| `substr(n)`   | n 文字目から末尾まで                                | `{{name \| substr(2)}}`   |
| `substr(n,m)` | n 文字目から m 文字分                               | `{{name \| substr(1,3)}}` |
| `replace`     | 引数なし: 改行を空白に置換                          | `{{name \| replace}}`     |
| `replace(old,new)` | 文字列置換（全一致）                          | `{{name \| replace("-", "_")}}` |
| `urlencode`   | RFC 3986 percent エンコード（unreserved 以外を `%XX` へ） | `{{name \| urlencode}}`   |
| `default("fb")` | 空文字列なら代替値                               | `{{name \| default("fallback")}}` |
| `safe`        | HTML エスケープ無効化（`{{{var}}}` と同等）      | `{{name \| safe}}`        |
| `json`        | JSON エンコード（`to_json` の別名）              | `{{name \| json}}`        |
| `indent(n)`   | 改行後に n 個の空白を挿入                           | `{{name \| indent(4)}}`   |
| `pad(n)` / `pad(n,"s")` | n 文字幅までパディング                     | `{{name \| pad(8)}}`      |
| `pluralize("one","many")` | 数値が 1 なら第1引数、以外は第2引数      | `{{n \| pluralize("item","items")}}` |
| `repeat(n)`   | 文字列を n 回繰り返し                               | `{{name \| repeat(3)}}`   |
| `format("fmt")` | `std::format` / chrono `format` による書式化      | `{{age \| format("05")}}` |

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
| `zerofill(n)` | n 桁ゼロ埋め        | `{{age \| zerofill(5)}}` |
| `is_neg` | 負数なら `true`        | `{{age \| is_neg}}` |
| `eq(n)`  | n と等しいか           | `{{age \| eq(3)}}`  |
| `ne(n)`  | n と異なるか           | `{{age \| ne(3)}}`  |
| `gt(n)`  | n より大きいか         | `{{age \| gt(3)}}`  |
| `gte(n)` | n 以上か               | `{{age \| gte(3)}}` |
| `lt(n)`  | n 未満か               | `{{age \| lt(3)}}`  |
| `lte(n)` | n 以下か               | `{{age \| lte(3)}}` |
| `add(n)` | n を加算               | `{{age \| add(3)}}` |
| `sub(n)` | n を減算               | `{{age \| sub(2)}}` |
| `mul(n)` | n を乗算               | `{{age \| mul(10)}}` |
| `div(n)` | n で除算（切捨）       | `{{age \| div(3)}}` |

### 浮動小数点フィルター

| フィルタ | 説明 | 構文例 |
| -------- | ---- | ------ |
| `precision(n)` | 小数点以下 n 桁に丸め | `{{value \| precision(2)}}` |
| `round` / `round(n)` | 最近接丸め（n 桁、省略時 0 桁） | `{{value \| round(1)}}` |

### セクションフィルター（パイプライン）

セクションキーに `|` で連結し、反復する要素集合を加工します。最大 4 つまで連結可。

| フィルタ | 説明 | 構文例 |
| -------- | ---- | ------ |
| `reverse` | 要素順を反転 | `{{#items \| reverse}}` |
| `take(n)` | 先頭 n 個のみ | `{{#items \| take(3)}}` |
| `skip(n)` | 先頭 n 個を捨てる | `{{#items \| skip(2)}}` |
| `take_last(n)` | 末尾 n 個のみ | `{{#items \| take_last(2)}}` |
| `skip_last(n)` | 末尾 n 個を捨てる | `{{#items \| skip_last(2)}}` |
| `stride(n,m)` | n 個取得して m 個飛ばす繰り返し | `{{#items \| stride(2,1)}}` |
| `join("sep")` | 要素間に区切り文字列を挿入 | `{{#items \| join(", ")}}` |

### フィルターのチェーン

複数のフィルターを組み合わせ可能。左から右に順に適用されます。

```cpp
{{name | trim | upper}}        // "  hello  " → "HELLO"
{{name | left(10) | upper}}    // "hi" → "HI       "
{{age | abs | numify}}         // -1234567 → "1,234,567"
```

## WASI minimal 対応

Wasm など例外が利用できない環境でも使用できます。

`wasm32-wasip1` + `wasi-sdk` の hosted 環境（`<iostream>`/`<chrono>` が利用可能）に対応しています。
CMake オプション `ENABLE_WASI_MINIMAL=ON`、またはマクロ `INJAMM_WASI_MINIMAL` を直接定義して有効化します。
自動検出は行いません。

`wasm32-unknown-unknown` (freestanding, `-nostdlib`) は hosted stdlib (`glaze`, `<string>` 等) に依存するため非対応です。

wasm32-wasip2 環境の対応は現時点では未検証です。

### 制限（無効化される機能）

`INJAMM_WASI_MINIMAL` は例外のみを無効化します。`wasm32-wasip1` は `wasi-sdk` の hosted 環境で `<iostream>`（`<istream>`/`<ostream>`）や `<chrono>` が利用可能なため、`INJAMM_NO_BYTECODE_IO` / `INJAMM_NO_CHRONO` は定義されません（`frozenchars` の `FROZENCHARS_WASI_MINIMAL` と同様）。

`wasm32-wasip2` 環境の対応は現時点では未検証です。wasi-sdk が正式対応したら検証予定です。

| 機能                                                       | 理由                                          |
| ---------------------------------------------------------- | --------------------------------------------- |
| Catch2 テスト（`injamm_tests`）                             | 例外必須のため無効（`wasi_minimal_smoke` のみ） |

通常ビルド（`ENABLE_WASI_MINIMAL=OFF`）では `format` / `enum` / Catch2 は有効です（WASI minimal でも `format` / `enum` は `trap/abort` フォールバックで有効。不正フォーマットは `trap`）。

`INJAMM_NO_BYTECODE_IO` / `INJAMM_NO_CHRONO` / `INJAMM_NO_FMT` / `INJAMM_NO_ENUM_REGISTRY`
の各サブガードは個別に手動定義可能です。`WASI_MINIMAL` 定義時に自動で無効になるサブガードはありません（`FMT`/`ENUM` とも `trap/abort` フォールバックで有効）。

### 例外なし（WASI minimal のみ）対応

例外は `ENABLE_WASI_MINIMAL=ON` のときのみ無効化されます（`-fno-exceptions -fno-rtti` 付与）。
通常ビルド（`OFF`）では例外有効で `throw` / `try/catch` が使用可能です。

- **実装**: `config.hpp` で `INJAMM_WASI_MINIMAL` 定義時に `INJAMM_NO_EXCEPTIONS` を自動定義し、`INJAMM_HAS_EXCEPTIONS` は `0`、`INJAMM_THROW` は `__builtin_trap()`（wasm では `unreachable`）/ `std::abort()` に展開。`bytecode_io.hpp` の `try_reserve`/`try_assign` は `INJAMM_HAS_EXCEPTIONS` で分岐（有効時は `try/catch`、無効時は `max_size` チェック）。`serialize_value.hpp` の `vformat` も `HAS_EXCEPTIONS` で `try/catch` を分岐。通常ビルドでは通常の例外機構がそのまま動作します。
- **WASI 時のみ無効な機能**: `frozenchars` の `FrozenString` (`_fs`) — 例外に依存するため `WASI_MINIMAL` 時のみ無効（`INJAMM_NO_FMT` 相当）。通常ビルドでは有効です。`format`（`std::format`/`fmt`）と `enum`（`enchantum`）は WASI minimal でも有効です（`_GLIBCXX_THROW_OR_ABORT`/`FMT_THROW=assert_fail`/`ENCHANTUM_THROW=trap` にフォールバック。不正フォーマットは `trap/abort`）。
- **制約（WASI 時のみ）**: NTTP のチャンク溢れ（`ct_parsed_template` 容量超過）やネスト深度超過の診断は `trap/abort` に劣化します（`MaxChunks = Tmpl.size()+1` で通常到達不能のため実害は軽微）。`vector::reserve` の OOM は `syntax_error` ではなく `trap/abort` になります（事前の `max_*` チェックで不正バイト列の多くは `syntax_error` で弾かれます）。

```bash
# 通常ビルド（例外有効）
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=~/vm/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build && ctest --test-dir build -V  # injamm_tests + wasi_minimal_smoke

# WASI minimal（例外なし、ホストコンパイラで検証）
cmake -B build-wasm -S . -DENABLE_WASI_MINIMAL=ON \
  -DCMAKE_TOOLCHAIN_FILE=~/vm/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build-wasm && ctest --test-dir build-wasm -V  # wasi_minimal_smoke のみ

# wasm32-wasip1（wasi-sdk + vcpkg triplet、CI の linux-wasi-minimal と同じ構成）
# triplets/wasm32-wasip1.cmake が /opt/wasi-sdk または ~/vm/wasi-sdk を chainload する
cmake -B build-wasi -S . -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=~/vm/vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DVCPKG_TARGET_TRIPLET=wasm32-wasip1 \
  -DVCPKG_OVERLAY_TRIPLETS=$PWD/triplets \
  -DENABLE_WASI_MINIMAL=ON
cmake --build build-wasi
file build-wasi/test_wasi_minimal  # WebAssembly
```

wasm32 では glaze 7.8.3+（8.3.0でも未修正）の `atoi.hpp` が MSVC 組み込みの `_umul128` を未修飾で呼ぶため、`config.hpp` でグローバルな `_umul128` を補っています（glaze 上流修正後に削除予定）。

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

// 名前付き partial のみレンダリング
auto partial = bc.render(data, "sidebar");

// @var 定数置換付き
std::map<std::string, std::string, std::less<>> c{{"f", "name"}};
auto bc2 = injamm::engine<User>("{{@var(f)}}", c);
```

### `injamm::bind<"name">(value)`

複数のコンテナや値を NTTP 名でバインドし、`injamm::render` で利用可能なコンテキストを生成します。

```cpp
auto ctx = injamm::bind<"items", "user">(items, user);
auto html = injamm::render<kTmpl>(ctx);
```

単一の値の場合は `"_"` という名前で自動的にバインドされます。

```cpp
auto ctx = injamm::bind(value); // "_" としてバインド
```

### Template Partials（engine<T> と NTTP の両方で可）

同一テンプレート内で名前付きの断片（partial）を定義し、複数回呼び出すことができます。`#partialdef` / `#partial` は engine<T> と NTTP `render<...>` のどちらでも使用できます。部分描画は `engine<T>::render(data, "name")` または NTTP の `injamm::render_partial<tmpl>(data, "name")` で行えます（HTMX 等の部分更新に有用）。
partial はプリコンパイル方式で保持されるため、`render(data, "name")` で特定の partial だけを選択的にレンダリングすることも可能です（HTMX 等の部分更新に有用）。

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

// フルレンダリング
auto html = bc.render(data);

// sidebar 部分のみレンダリング（HTMX Partial 等）
auto sidebar_html = bc.render(data, "sidebar");
```

`{{#partialdef name}}...{{/partialdef}}` で partial を定義し、`{{#partial name}}` で現在のコンテキストで描画します。
partial は前方参照可能（定義より前で呼び出せる）。partial 内から別の partial も呼び出せます。

### NTTP partial (`{{> partial}}`)

NTTP `render` の entry pair は `@var` だけでなく partial 展開にも使えます。entry pair の**キーが partial 名**、**値が partial の本文**に対応し、テンプレート文字列の外部から断片を注入できます。

> runtime `engine<T>` でも `{{> name}}` は利用できます。その場合は engine コンストラクタのレジストリ経由（`{ injamm::make_partial<T>("header", "...") }`）で partial を注入します。

```cpp
auto ctx = injamm::bind<"title", "name">(title, name);
auto html = injamm::render<
  "{{> header}} {{name}} {{> footer}}",
  "header", "<h1>{{title}}</h1>",
  "footer", "<footer>{{title}}</footer>"
>(ctx);
```

`{{> name}}` はテンプレート文字列の**外**（entry pair）から本文を持ってくるのに対し、`#partialdef name` は同一テンプレート文字列**内**で定義します。どちらも内部は同じ partial メカニズム（`call_partial`）で実行されるため、前方参照や partial 内からの partial 呼び出しも共通して利用できます。

### `injamm::save_bytecode(bc, ostream)`

バイトコードをバイナリ形式でストリームに保存します。戻り値は `error_code`。

```cpp
auto ec = injamm::save_bytecode(engine.get_bytecode(), std::ostringstream{});
```

### `injamm::load_bytecode<T>(istream)`

バイナリ形式からバイトコードを読み込み、`field_index` を `T` の glaze リフレクションで再解決します。戻り値は `expected<bytecode>`。

```cpp
auto bc = injamm::load_bytecode<User>(std::istringstream{data});
auto eng = injamm::engine<User>(std::move(*bc));
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
| 8   | division_by_zero | 除数ゼロエラー           |
| 9   | out_of_memory  | メモリ不足                 |
| 10  | invalid_format | 不正なフォーマット文字列   |

実行時無例外保証: 実行時パスは例外を送出しません。OOM は `out_of_memory`、不正フォーマットは `invalid_format` を `expected` で返します。コンパイル時診断の `INJAMM_THROW` は維持されます（`test_noexcept` で検証、throw/try-catch の CI grep 検査あり）。

### エラー診断・フォーマット（`formatError` / `error_ctx::format`）

エラー発生時にテンプレートの該当行・列番号および周辺コードスニペット付きで診断メッセージを整形できます。

```cpp
auto res = eng.render(data);
if (!res) {
  // コンパイラ風の診断メッセージを取得 (例: "template.html:2:8: error: Unknown key: foo\n  ...")
  std::cerr << res.error().format(tmpl_string, "template.html");

  // またはスタンドアロン関数を利用
  std::cerr << injamm::formatError(tmpl_string, res.error(), "template.html");

  // 行番号・列番号の個別取得
  auto loc = injamm::getSourceLocation(tmpl_string, res.error().position);
  std::cerr << "Line: " << loc.line << ", Column: " << loc.column << "\n";
}
```

## 注意事項

- `render<fixed_string>` の戻り値型 `expected<std::string>` は、GCC 16 の `[[nodiscard]] expected<void, error_ctx>` と衝突する可能性があります。必要に応じて `void` 特殊化を無視してください。
- GCC 以外のコンパイラでは `ENABLE_THREADED_DISPATCH` を OFF にしてください。(デフォルトは自動で OFF になります)
- **テンプレート文字列と @var / partial の型は一致させる必要があります。** `FrozenString`（`_fs` リテラル）をテンプレートに使う場合、`render<...>` / `render_partial<...>` の追加テンプレート引数（@var の key-value ペアや partial 名/本文）も同じ型で統一する必要があります。テンプレートパラメータパックでは異なる型の混在ができないためです。

  ```cpp
  // OK: すべて fixed_string（文字列リテラル → consteval 構築）
  auto constexpr t = injamm::fixed_string("{{@var(x)}}");
  injamm::render<t, "x", "value">(ctx);

  // OK: すべて FrozenString
  using namespace frozenchars::literals;
  auto constexpr t = "{{@var(x)}}"_fs;
  injamm::render<t, "x"_fs>(ctx);

  // NG: FrozenString テンプレートに文字列リテラルを混在
  injamm::render<t, "x", "value">(ctx);  // コンパイルエラー
  ```

  `injamm::fixed_string(...)` で CTAD 推定が可能なため、`FrozenString` の代わりに `fixed_string` で統一すれば文字列リテラルをそのまま使えます。

  ```cpp
  auto constexpr t   = injamm::fixed_string(R"({{@var(x)}})");
  auto constexpr sub = injamm::fixed_string(R"(<tr>{{id}}</tr>)" | frozenchars::ops::minify_html);
  injamm::render<t, "x", sub>(ctx);  // OK: "x" は文字列リテラルのまま
  ```

## スレッド安全性

### NTTP コンパイル時レンダリング (`render<fixed_string>`)
- **スレッドセーフ**: 同じテンプレートに対する複数スレッドからの並列レンダリングは安全です
- コンパイル時に生成されるバイトコードは `static` で保持され、読み取り専用アクセスのみ行われます
- 各レンダリング呼び出しは独立したスタック上で実行されるため、共有状態の変更はありません

### バイトコード VM (`engine<T>`)
- **スレッドセーフ**: `engine<T>` インスタンスは複数スレッドからの並列 `render()` 呼び出しに対応しています
- `engine<T>` のコンストラクタで生成されるバイトコードは不変（immutable）であり、実行時の変更はありません
- `render()` メソッドは `const` で宣言されており、内部状態を変更しません
- 異なるデータに対する並列レンダリングは安全に実行できます

```cpp
// 並列レンダリングの例
injamm::engine<User> engine("Hello {{name}}!");

std::vector<std::thread> threads;
std::vector<std::string> results(10);

for (int i = 0; i < 10; ++i) {
  threads.emplace_back([&engine, &results, i]() {
    auto r = engine.render(User{"User" + std::to_string(i), 20 + i});
    if (r) {
      results[i] = *r;
    }
  });
}

for (auto& t : threads) {
  t.join();
}
```

### 注意点
- 並列レンダリング時、出力先バッファ（`std::string& out`）は各スレッドで独立している必要があります
- 同じ `std::string` インスタンスを複数スレッドから同時に書き込むことは避けてください
