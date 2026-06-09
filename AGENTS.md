# injamm — Agent Guide

## これは何か

ヘッダオンリー C++26 テンプレートエンジン（Mustache/inja サブセット）。2つのレンダリング API を提供:
- **Bytecode VM** (`engine<T>`) — 実行時コンパイル、全機能（セクション、if/else、`@index`/`@first`/`@last`、ネストパス）
- **NTTP コンパイル時** (`render<fixed_string>`) — `{{var}}` のみ、セクション/if 非対応

## ビルド & テスト

```sh
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=~/vm/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build
ctest --test-dir build -V
./build/injamm_tests           # 直接実行 (Catch2)
```

CMake オプション: `INJAMM_ENABLE_THREADED_DISPATCH`（デフォルト ON、GCC のみ）、`INJAMM_BUILD_TESTS`、`INJAMM_BUILD_EXAMPLES`。

## 重要な規約

- **C++26 必須**、GCC 16+ 推奨。
- **すべてのコンテキスト型に `glz::meta<T>` 特殊化**が必要 — 例外なし。
- clang-format: `LLVM` スタイル、`IndentWidth: 2`、`ColumnLimit: 200`、`PointerAlignment: Left`。
- clang-tidy 命名: 型/変数は `lower_case`、関数は `camelBack`、定数は `upper_case`。
- 後置戻り値型禁止（`modernize-use-trailing-return-type` 無効）。
- `.clangd` は `-std=c++23`（CMake の `cxx_std_26` と乖離 — 標準変更時は両方を更新）。
- `build.sh` は `~/vm/vcpkg` の vcpkg を使用、スタティックトリプレット `x64-linux-static`。

## API の注意点

- `render()` は `expected<std::string>` を返す（`std::expected<std::string, error_ctx>` のエイリアス）。
- エラー構造体は `.position`（バイトオフセット）、`.ec`（error_code 列挙型）、`.custom_error_message` を持つ。
- `{{var}}` = HTML エスケープあり、`{{{var}}}` = 生出力（ステンシルモード）。
- セクションの真偽: 非ゼロ数値、非空文字列、非ヌルポインタ = true。
- `{{^section}}` = 逆セクション（偽/空のときに描画）。
- テンプレートファイル: `include/injamm.hpp` が一次エントリポイント。`include/injamm/escape_hatch.hpp` が `engine`/`render` を公開。

## 依存関係

- `glaze`（必須、vcpkg: `~/vm/vcpkg`）
- `catch2`（テストのみ、vcpkg）

## Git なし

git リポジトリではない。`.gitignore` / `.gitattributes` なし。
