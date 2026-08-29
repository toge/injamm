# injamm — Agent Guide

## これは何か

ヘッダオンリー C++23 テンプレートエンジン（Mustache/inja サブセット）。3つのレンダリング API を提供:
- **Bytecode VM** (`engine<T>`) — 実行時コンパイル、全機能（セクション、if/else、`loop.index`/`loop.is_first`/`loop.is_last`、ネストパス）
- **NTTP コンパイル時** (`render<fixed_string>`) — コンパイル時パース、全機能対応
- **AOT コード生成** (`injamm_codegen`) — バイトコード/テンプレートから glaze 非依存の C++ レンダリング関数を生成。VM より高速な直接フィールドアクセス

## ビルド & テスト

```sh
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=~/vm/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build
ctest --test-dir build -V
./build/injamm_tests           # 直接実行 (Catch2)
```

CMake オプション: `ENABLE_THREADED_DISPATCH`（デフォルト ON、GCC のみ）、`BUILD_TEST`、`BUILD_EXAMPLE`、`BUILD_FUZZ`、`BUILD_UTIL`、`ENABLE_ENUM`。

## 重要な規約

- **C++23 必須**（`std::expected`）、GCC 14+ 推奨。
- clang-format: `LLVM` スタイル、`IndentWidth: 2`、`ColumnLimit: 200`、`PointerAlignment: Left`。
- clang-tidy 命名: 型/変数は `lower_case`、関数は `camelBack`、定数は `upper_case`。
- 後置戻り値型禁止（`modernize-use-trailing-return-type` 無効）。
- `.clangd` と CMake はともに `c++23` / `cxx_std_23` で統一。
- `build.sh` は `~/vm/vcpkg` の vcpkg を使用、スタティックトリプレット `x64-linux-static`。

## API の注意点

- `render()` は `expected<std::string>` を返す（`std::expected<std::string, error_ctx>` のエイリアス）。
- エラー構造体は `.position`（バイトオフセット）、`.ec`（error_code 列挙型）、`.custom_error_message` を持つ。
- `{{var}}` = HTML エスケープあり、`{{{var}}}` = 生出力（ステンシルモード）。
- セクションの真偽: 非ゼロ数値、非空文字列、非ヌルポインタ = true。
- `{{^section}}` = 逆セクション（偽/空のときに描画）。
- テンプレートファイル: `include/injamm.hpp` が一次エントリポイント。`include/injamm/escape_hatch.hpp` が `engine`/`render` を公開。`include/injamm/ct_exec.hpp` が NTTP のコンパイル時アンロール実行器（単純テンプレートの高速パス、非該当は VM フォールバック）。

## 依存関係

- `glaze`（必須、vcpkg。fast_float は glaze 内蔵のものを利用）
- `fmt`（`format` フィルタの AppleClang フォールバック + `injamm_bench_format` の FMT_COMPILE 計測用。vcpkg、osx | linux）
- `catch2`（テストのみ、vcpkg）
- `frozenchars`（GCC NTTP テストのみ、vcpkg）
- `enchantum`（`ENABLE_ENUM`=ON 時、vcpkg）

※ sqlite3 拡張は別リポジトリ（`~/src/injamm-sqlite3`）に分離済み。本体への依存は `find_package(injamm)` 経由。

## ドキュメント更新ポリシー（必須）

- ソースコード（`include/injamm/**`, `src/**`, テンプレート構文・フィルタ・API の追加/変更/削除）を修正した際は、必ず以下3ファイルの更新要否を検討し、必要なら同一コミットで更新すること:
  - `README.md` — 機能一覧・構文一覧・フィルタ表・API 概説
  - `SYNTAX.md` — 全テンプレート構文の詳細リファレンス（サンプル・出力付き）
  - `CHANGELOG.md` — 変更履歴（日付降順、新しい順）
- 判断基準: 公開 API / テンプレート構文 / フィルタ / エラーコード / ビルドオプション / 挙動の変更は **必ず** 3ファイルを確認。内部リファクタのみなら `CHANGELOG` は任意だが `README`/`SYNTAX` に影響がないかは依然として確認する。
- レビュー時は `SYNTAX.md` と `README.md` の構文一覧・フィルタ表が `include/injamm/parse.hpp` / `bytecode_fwd.hpp` / `filters.hpp` / `bytecode_compile.hpp` の実装と一致していることを照合する。
