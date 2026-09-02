# Changelog

## 2026-09-02

- `ci: linux-wasi-minimal ジョブを wasi-sdk 34 + vcpkg `wasm32-wasip1` triplet でのビルドに移行` — `~/src/frozenchars` と同じ構成。`triplets/wasm32-wasip1.cmake`（chainload で `/opt/wasi-sdk` または `~/vm/wasi-sdk` を選択）を追加し `.gitignore` の `*.cmake` から除外。`vcpkg.json` の `catch2` を `emscripten | !wasm32`、`enchantum` を `!wasm32` に限定。生成物が WebAssembly であることを `file` で検証
- `fix: wasm32 ビルドの阻害要因を除去` — glaze 7.8.3 の `atoi.hpp` が 32bit 非 MSVC で未修飾 `_umul128` を呼ぶため `config.hpp` に `__wasm__` 限定のグローバル `_umul128` シムを追加（上流修正後に削除）。`injamm_benchmark` / `injamm_codegen_bench` の `-march=native` を WASI / Emscripten では付与しないよう変更。`float_filter_name` に `round` の case を追加（-Wswitch 警告解消）
- `fix: 例外無効化を ENABLE_WASI_MINIMAL=ON のみに限定` — これまで通常ビルドも常時 `-fno-exceptions` にしていたが、`ENABLE_WASI_MINIMAL=OFF` では例外有効に戻し `USE_FMT`/`ENABLE_ENUM`/`BUILD_TEST`（Catch2）を有効化。`ENABLE_WASI_MINIMAL=ON` 時のみ `-fno-exceptions -fno-rtti` と `INJAMM_NO_EXCEPTIONS`/`INJAMM_WASI_MINIMAL` を付与し `USE_FMT`/`ENABLE_ENUM` を自動無効化、`BUILD_TEST` は `freestanding_smoke` のみに。`config.hpp` は `INJAMM_HAS_EXCEPTIONS` / `INJAMM_THROW` を条件分岐に戻し、`bytecode_io.hpp` の `try_reserve`/`try_assign` と `serialize_value.hpp` の `vformat` も `HAS_EXCEPTIONS` 分岐を復元。`INJAMM_NO_EXCEPTIONS` CMake オプションは削除。README のオプション表と「例外なし」章を「WASI minimal のみ」に修正
- `refactor: FROZENCHARS_WASI_MINIMAL に合わせ INJAMM_WASI_MINIMAL / ENABLE_WASI_MINIMAL に統一` — 一時的に `WASM_MINIMAL` へリネームしていたが `~/src/frozenchars` が `FROZENCHARS_WASI_MINIMAL` / `ENABLE_WASI_MINIMAL` のため `WASI_MINIMAL` に戻し、旧 `WASM` 別名は残さず完全置換
- `feat: 例外なし（-fno-exceptions / WASM freestanding）対応` — `config.hpp` に `INJAMM_HAS_EXCEPTIONS` / `INJAMM_THROW` / `injamm_trap()` を追加し、`__cpp_exceptions` 未定義時は自動で例外なしモードに切替。`ct_chunk.hpp` 16箇所・`ct_parse.hpp` 1箇所の `throw` を `INJAMM_THROW` に、`bytecode_io.hpp` の `vector::reserve` / `string::assign` の 15箇所 `try/catch` を `try_reserve` / `try_assign` ヘルパ（`INJAMM_HAS_EXCEPTIONS` 分岐、例外なし時は `max_size` チェック）に集約（例外あり時の挙動は維持、catch 本体は 3 ヘルパ内に集約）。`serialize_value.hpp` の `vformat` 2箇所も `INJAMM_HAS_EXCEPTIONS` 分岐。CMake に `option(INJAMM_NO_EXCEPTIONS)` を追加し `INTERFACE` で `-fno-exceptions -fno-rtti` / `INJAMM_NO_EXCEPTIONS` 定義を付与（`INJAMM_NO_EXCEPTIONS=ON` 時は Catch2 要求のため `BUILD_TEST` を自動 OFF、freestanding_smoke は維持）。`INJAMM_NO_EXCEPTIONS` 下では NTTP 溢れ診断が `trap/abort` に劣化、`vector::reserve` OOM は `trap`（不正バイト列の多くは事前 `max_*` で `syntax_error`）。README に「例外なし（WASM freestanding）対応」章を追加

## 2026-09-01

- `fix: emscripten で FREESTANDING モードが誤自動検出される問題を修正` — emscripten は `__wasm__` 定義かつ `__wasi__` 未定義のため `config.hpp` の自動検出が発火し、CI の emscripten ジョブで ostream 版バイトコード I/O と `error_code` の `operator<<` が除外されて `test_injamm.cpp` がコンパイルエラーになっていた。`__EMSCRIPTEN__` 定義時はホスト環境（完全な libc++）として自動検出対象外に変更。wasm32-unknown-unknown（`__wasm__` のみ）の自動検出と `ENABLE_WASI_MINIMAL` / `INJAMM_WASI_MINIMAL` 明示指定は従来どおり
- `feat: wasm32-unknown-unknown 用の FREESTANDING モードを追加` — `config.hpp` を新設し、wasm32-unknown-unknown（`__wasm__` 定義かつ `__wasi__` 未定義）で `INJAMM_WASI_MINIMAL` を自動有効化。CMake オプション `ENABLE_WASI_MINIMAL` でも明示制御可能。有効時は `INJAMM_NO_BYTECODE_IO`（`<istream>`/`<ostream>` 版バイトコード I/O 除外、span 版は維持）/ `INJAMM_NO_CHRONO`（time_point シリアライズ除外、`is_chrono_time_point_v` 常に false で if-constexpr 分岐が消滅）/ `INJAMM_NO_FMT`（`<format>`/`fmt` 版 format フィルタ除外、no-op スタブで呼び出し箇所はコンパイル可能）/ `INJAMM_NO_ENUM_REGISTRY` が連動して ON。NTTP render / engine VM / 文字列フィルタ / セクション・if・loop / glaze フィールド解決は freestanding でも使用可能。README に「Freestanding 対応」章を追加

## 2026-08-31

- `bench: bench_format に runtime (engine VM) 計測を全ケース追加` — 1 var string/int/double, 2 vars ×3, 10 vars で `engine<T>`（VM dispatch）も併計測。NTTP は `ct_exec.hpp` の専用アンロールで VM より 1.5–3.2 倍高速（10 vars 3.15x）なことを定量化。`USE_CASES.md §6` を NTTP vs format / engine vs format / NTTP vs engine の 3 表に再構成し、`docs/bench_format_2026-08-31.md` を追加（`2026-08-30` の 7回中央値は保持）

## 2026-08-30

- `feat: 親スタック解決（Mustache 互換の暗黙参照）を追加` — セクション本体内の変数参照を「現在要素 → 内側のセクション → ルート」の順で**コンパイル時**に解決。実行時コストはゼロ（明示 `{{root.field}}` と同一のホットパス、ベンチで同等〜同等以上を確認）。VM は実証済みルート参照で要素走査をスキップ、NTTP は未実証モードで要素走査後にルート走査、codegen はルート直接アクセスを生成。制限: 型不明コンテキスト（map 要素等）内では暗黙解決なし、ネストしたルートパス/外側セクション要素への暗黙参照は engine<T> のみ。バイトコード形式を v6 に更新（root_fallback フラグ追加）
- `docs: README 冒頭を 2→3 API に修正し AOT コード生成（injamm_codegen）を追記、CLI 章を injamm_bc/injamm_codegen 統合に再構成、AGENTS.md も同期`

## 2026-08-27

- `perf: 4施策適用(literal coalesce + strip/exists 早期 return + html_escape_into の冗長 string_view 構築削除)` — 11行追加/1行変更で VM 経路を -3〜-12% 改善。`multi_filter -11.96%`, `BC nested_2level -6.85%`, `NTTP nested_3level -6.61%`, `NTTP wide partial -9.28%`, `engine render reuse buffer -8.61%` 他。詳細: `docs/perf_proposal_2026-08-27.md`

## 2026-08-22

- `urlencode` フィルタを追加（RFC 3986 percent エンコード、engine<T> / NTTP 両対応）
- `loop.is_even` / `loop.is_odd` を追加（zebra ストライプ用、`{{loop.is_even}}` 変数出力・`{{#loop.is_even}}` セクション・`{{^loop.is_even}}` 逆セクション・`{{#if loop.is_even}}` 条件式のすべてに対応、`loop.parent.*` 版も含む）

## 2026-08-18

- `strip` / `lstrip` / `rstrip` フィルタを追加（`trim` / `ltrim` / `rtrim` の別名）
- `trim` / `ltrim` / `rtrim` を Python 準拠の空白セット（space/tab/LF/CR/VT/FF）対応に拡張し、`trim("xy")` 形式の文字集合引数をサポート（※従来はスペースとタブのみ除去）

## 2026-07-05

- `7afab40` clang でのコンパイルエラーを修正

## 2026-07-04

- `eaf5f7c` パフォーマンス改善: リザーブ見積もりの精度向上と静的ディスパッチ化 / 高速パス最適化とバッファ再利用 API の追加

## 2026-07-03

- `67d57b5` if の条件文が定数評価できる場合にスキップする最適化を追加
- `274ff8a` Partial Template ライクな機能を追加

## 2026-06-30

- `28e2e15` injamm::sqlite3 に未実装だった内容を追加

## 2026-06-27

- `a53eb5d` enum サポート

## 2026-06-26

- `f39094a` engine のデフォルトコンストラクタを無効化
- `786e48b` オペコード探索の範囲外アクセスチェックを追加
- `81d648e` injamm::bind の引数なし命名を value から _ に変更
- `039de03` std::make_unique を外して微高速化
- `6a9918d` 文字列→整数変換のオーバーフロー対応
- `35a8f11` HTML エスケープ文字に対応
- `21464fa` 文字列内のエスケープ文字に対応

## 2026-06-25

- `81ba3ce` セクションに {{else}} を追加
- `7f5e3c5` 比較演算子・論理演算子・loop.parent.* を追加

## 2026-06-24

- `6e742ea` コンテナを直接変数にバインドする injamm::bind を追加
- `904df7f` 数値フィルター用の四則演算を追加
- `4a5c109` upstream で追加したフィルターを injamm-ext-sqlite3 に追加
- `e034bdc` 長さを返す .size に対応

## 2026-06-22

- `d1ca603` fmt を依存関係から削除

## 2026-06-21

- `3ec3f54` コンパイル時の異常系処理対応
- `28d46af` 数値パースの高速化とフィルター種別の拡充
- `87aa502` clang が frozenchars で動かない問題への対応

## 2026-06-20

- `6465181` injamm-sqlite3: sqlite3_stmt を直接レンダリングする別ライブラリを追加
- `3165937` injamm-sqlite3: コードレビュー修正を適用
- `cc1a4fa` パフォーマンス改善提案を実装
- `d103336` 配列へのインデックスアクセスを可能に

## 2026-06-18

- `a222c7a` @size, コメントフォーマット追加、#exists、比較演算子、{{~~}}、replace フィルタを実装
- `b7ea3fc` @index/@first/@last/@key を loop.* にリネーム (inja 互換)
- `9db4755` パス探索の性能向上

## 2026-06-17

- `e87fe6b` テンプレート内のコメントに対応
- `adb7620` @index1 に対応
- `ffe847b` trim_blocks / lstrip_blocks 機能を追加

## 2026-06-16

- `1b916b1` frozenchars がある場合、FrozenString を NTTP のテンプレート引数に指定可能に
- `b813420` @var の循環参照を防ぐ仕組みを追加

## 2026-06-12

- `9c35a15` ct_bytecode, string_ref, builder, to_bytecode を追加
- `c0e798d` ct_chunks_to_bytecode を render<> に組み込み
- `83ddb82` リテラルチャンクのコンパイル (emit_literal)
- `6200491` プレースホルダのコンパイル (emit_var/emit_litvar + @root.field)
- `47ff4c2` セクション/逆セクションのコンパイル (emit_section/emit_inverted)
- `37db45c` at_var/at_section のコンパイル
- `4f6ef9a` if/else のコンパイル (emit_if/emit_else/emit_endif)
- `d68d3c8` フィルター、break, continue のバイトコードコンパイル
- `4dad556` ct_render.hpp を削除、NTTP が CT バイトコード + VM エグゼキュータを使用するように変更
- `5f27b7c` NTTP の Bytecode を static 保持して高速化
- `cfce524` バイトコード実行の最適化
- `d63a19c` html_escape と filter を高速化

## 2026-06-11

- `7d4fdb8` ne, gt, gte, lt, lte 演算子を追加

## 2026-06-10

- `8db6882` @var プリプロセッシングユーティリティを追加
- `2765cfd` @var が {{{}}}  raw タグ内でも展開されるように修正
- `0405fe6` ConstMap 対応の bc_compile オーバーロードと error_ctx フィールドを bytecode 構造体に追加
- `8be815f` engine&lt;T&gt; に map ライクなコンストラクタとエラーチェックを追加
- `9f54087` @var(name) 展開対応の NTTP render オーバーロードを追加

## 2026-06-09

- `2ffa806` 実数用のフィルターを追加、include のヘッダ構成を整理
- `1c985de` bc_template を engine に変更
- `88d9dd4` 条件に filter を適用できる if を追加
- `b4672ec` 負の数の2進数表示を修正
- `abd4b06` セクションでの @変数指定に対応
- `e7cbe40` 不正なフィルターにエラーを返すようにした
- `66b2133` 並列レンダリングを考慮してレンダー時のフィルター処理用バッファを bc_executor 内に持たないようにした
- `37bd698` std::optional 対応
- `a3f3374` break, continue を追加
- `e8a8e34` NTTP 版に不足していた機能を追加
- `39c38f4` バイトコード出力機能を追加した
- `685d87c` zerofill フィルターを追加
- `08e347d` continue_flag のリセット漏れに対応
- `6f22321` double 版の abs, neg で to_chars を利用するように
- `349bc73` 一度に連結できるフィルターの最大数を超過した場合にエラーメッセージを出すように
- `3ef2db8` parse_into を失敗した場合にエラーメッセージに詳細情報を含めるようにした
- `da93687` filter を外出しにして、Bytecode と NTTP でフィルターの実装を共通化
- `b172811` abs/neg が LLONG_MIN を正しく評価できない問題を修正
- `03dba13` ゼロ除算に対応
- `a64bbb0` std::map ライクなデータ対応
- `a508a98` std::set ライクなデータ対応

## 2026-06-08

- `ab75ecc` initial commit
- `00af247` @root, @key を追加
- `3c9cc83` 色々加工用関数を追加
