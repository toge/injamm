# Changelog

## 2026-11-25

- `docs: Issue11 パリティ最終同期` — `injamm_codegen` 再生成結果を反映し、README/SYNTAX の AOT 説明を VM/NTTP と一致（break/continue の安全な生成境界、直接比較の右辺（変数/文字列）明記、文字列セクションは真なら 1 回評価、二段解決の既存規則を維持）させた

## 2026-11-24

- `fix: NTTP render がセクションの構文エラーを黙って無視していた問題` — TLA+ モデル検査（`tlaplus/`）が検出した VM/CT 不整合を修正。CT パーサの閉じタグ探索を VM (`compile_body_impl` の `expected_close` 検証) と同じスタック走査に揃え、`{{#a}}{{/b}}`（取り違え → `syntax_error`）、未閉鎖セクション（`unexpected_end`）、トップレベルの余分な閉じタグ（`stray closing tag`）が `engine<T>` と `render<>` の両方で同じエラーになる。`{{~/if~}}` 等チルダ付き閉じタグも VM の前処理と同じ意味で一致（旧 CT 実装は未閉鎖+閉じタグ無視の 2 バグが相殺して通っていた）
- `fix: VM 前処理 strip_bang_comments がコメント内の {{ }} を考慮せず最初の }} で切っていた問題` — `{{! {{name}} }}x` が engine で ` }}x`、NTTP render で `x` になる出力不一致を解消。CT パーサと同じネスト考慮のコメント終端判定に揃えた（差分プローブで検出、crosscheck テスト追加）
- `fix: VM がネスト内の mismatched closing tag を外側の unexpected_end で上書きしていた問題` — `{{#a}}{{#b}}{{/a}}` が engine で `unexpected_end`、NTTP render で `syntax_error` になるエラー種別の不一致を解消。`compile_section` / `compile_inverted` / `compile_body_impl` のエラー設定に first-wins ガードを追加し、SYNTAX.md どおり取り違え = `syntax_error` が最優先で報告される（差分プローブで検出、回帰テスト追加）
- `fix: トップレベル / loop.* 本体内の {{else}} の扱いが VM と NTTP で食い違う問題` — トップレベル `{{else}}` が engine で `syntax_error`(stray)、NTTP で `unknown_key` になる種別不一致、`{{#loop.index}}A{{else}}B{{/loop.index}}` が engine で `syntax_error`、NTTP で無エラーになる不一致を解消。CT メインパーサに else を stray 扱いでエラー化する分岐を追加
- `fix: {{~else~}}（チルダ付き else）が NTTP で else として認識されない問題` — `find_toplevel_else` がチルダ正規化を行っておらず、`{{#name}}a{{~else~}}b{{/name}}` が engine で成功・NTTP で `unknown_key` エラーになっていた。本体パースと同じチルダ正規化を追加
- `test: TLA+ 反例の回帰テストを追加` — `tests/test_injamm_tla_counterexample.cpp`（TLC が列挙した反例 `{{#a}}{{/b}}` / `{{#a}}` を回帰として固定）

## 2026-11-20

- `fix: injamm_codegen がスカラー/optional の逆セクション（{{^bool}} 等）でコンパイル不能なコードを生成する問題` — `value_empty` に bool/算術型/enum/optional/reflectable の分岐を追加し、VM (`do_inverted`) と同一の「空/偽のとき本体 1 回」判定を生成
- `fix: injamm_codegen がセクションの {{else}} で構文エラーになるコードを生成する問題` — `{{#section}}A{{else}}B{{/section}}` を本体ラムダ + else ラムダ + 空判定ディスパッチとして生成。ループ種別（フィルタ/インデックス参照）は `if (!value_empty(...))` 事前ガードで対応。逆セクションの `{{else}}` もラムダ構造に統一
- `fix: injamm_codegen が std::optional のセクションをコンテナ扱いしてコンパイル不能になる問題` — `codegen_optional_section_v` 分岐を追加し、VM と同じ `has_value()` → `*opt` をスコープに本体 1 回を生成
- `fix: injamm_codegen がループ内のグローバル partial をルート固定で解決する問題` — prefix 固有の内部 partial dispatcher `(<value>, <root>, <sink>)` を生成し、本体は「値 → ルート」の二段解決（`requires` 式）に。公開 `render_partial<P>(data, out)` のシグネチャは変更なし。ループ内の要素に無いキーのセクション/逆セクション/変数参照も同じ二段解決でルートへフォールバック
- `fix: injamm_codegen が -n オプションを無視する問題` — 名前空間が `generated` 固定だったのを `-n` 指定を反映するように修正（既定値は従来どおり `generated`）
- `fix: codegen partial の通常参照・ドットパス・フィルタ・条件が「現在値 → ルート」で統一されない問題` — partial バイトコードを二段解決メタデータ付きで保持し、simple VM/NTTP 高速パスも無効化。AOT の全参照 opcode、section filter/index fast path、VM の filtered 解決を同じ順序へ修正
- `fix: 生成ヘッダを同一 TU で使うと共通ヘルパ・partial dispatcher・include guard が衝突する問題` — 共通ヘルパを生成名前空間の外へ移し、ガードとキー名を一意な16進エンコードに変更。partial dispatcher は prefix 固有にし、公開 `render_partial` に未知名 fallback、同名 partial 選択用の `<prefix>_partial` を追加
- `fix: README 記載の injamm_codegen 長形式オプション` — `--input` / `--expr` / `--type` / `--output` / `--namespace` / `--prefix` / `--help` を CLI で受理
- `test: codegen ライブテスト（生成 → コンパイル → 実行 → VM 比較）を追加` — `test_codegen_live` がビルド時に各ケースのヘッダを `injamm_codegen` で生成するため、生成ヘッダが壊れているとビルドが失敗する。同梱 `render*.hpp` は `test_codegen/regenerate.sh` で再生成
- `fix: 生成コードの二段解決（値 → ルート フォールバック）が Clang / MSVC でコンパイルできない問題` — ジェネリックラムダ内の `if constexpr (requires { _item1.key; })` の破棄側（`data.key`）は、クロージャ構築時に実体化されて検証されるため、ルート型にキーが無いとコンパイルエラーになっていた（GCC は検証しないため検出漏れ）。キーごとの二段解決ヘルパ関数テンプレート（`cg_get_*`）を名前空間スコープに生成し（関数テンプレートの if constexpr は破棄側が遅延実体化される）、セクション ディスパッチの else 側はラムダのルート引数（`_rtN`）経由の依存式に変更
- `fix: MSVC 19.52 で NTTP render がコンパイルできない問題` — `ct_exec.hpp` の `ct_emit_var` が即時実行ラムダ内から外側関数の `constexpr` ローカル参照（`ref`）を参照していた。MSVC 19.52 はこれを定数評価で拒否する（19.51 は受理）。IIFE を constexpr 関数テンプレート `ct_key_has_dot` に置き換え
- `bench: codegen_bench に逆セクション / セクション else / optional セクション / ループ内 partial のワークロードを追加` — `render_d`〜`render_g`（`gen.sh` で再生成）。#10 対応の新旧コードジェネレーターでスループットを比較し、既存ケース（セクション/フィルタ/bool 出力）は誤差範囲、逆セクション・partial も ns/byte 正規化で劣化なしを確認

## 2026-11-19

- `fix: -fno-exceptions で format の幅/精度 > 65535 が SIGABRT する問題を修正` — 自家バリデータ (`is_valid_format_spec`) は通すが libstdc++ の `vformat` が `format_error`（no-exceptions では abort）を投げる幅/精度を事前に `invalid_format` として拒否
- `fix: numify / round の未検証 double→long long キャストによる UB と繰り上がり欠落` — `1e19 | numify` のゴミ出力・`1e19 | round` の符号消失を解消。numify は全体を `to_chars(fixed, prec)` で丸めてから整数部にカンマを挿入する方式に変更し、`1.9999999999` → `"2.000000"` のような繰り上がり欠落も修正（`codegen_helpers.hpp` の drift 実装も同期）
- `fix: {{#if x == y}} の変数右辺が常に 0 と比較されていた問題` — `compare_rhs_field_index` / `compare_rhs_text` を実行時に解決（value_ → ルート → ループ束縛）。数値/文字列/大小比較すべてが機能し、README の「右辺は変数」という記載に実装が追いついた
- `fix: {{^x}}（逆セクション）のみルートフォールバックが未実装だった問題` — `do_inverted` に `do_section` と同じ二段走査を追加。`{{#items}}{{^verbose}}...{{/verbose}}...{{/items}}` が `{{#if verbose}}` と同じ判定になる
- `fix: {{#count}}（数値/enum セクション）が常に偽扱いだった問題` — `do_section` に算術型/enum 分岐を追加し、`{{^count}}`・`{{#if count}}` と「非ゼロ = 真」で統一（SYNTAX.md §5 の記載通りに）
- `fix: {{#break}} 時に join の区切り文字が末尾に残る問題` — join 判定が break_flag を見る前に走っていたため `a, b, ` のようになるのが `a, b` に
- `fix: NTTP render が未知フィルタを黙って無視していた問題` — CT パーサで `unknown_filter` を `ct_bc.error` に記録（VM の `bc_compile` とパリティ）。`render<"{{name | zzz}}">` が engine と同じ `unknown_filter` エラーを返す
- `fix: 同一キー・フィルタ付きネストセクションで NTTP と VM のパースが食い違う問題` — CT 側の閉じタグ探索が `{{#key | filter}}` 形の同一キー開始タグを深さカウントに含めていなかった。`{{#items}}{{#items | reverse}}...` が両 API で一致。空白なしの `{{#key|filter}}` 形も開始タグとして扱うよう修正
- `fix: injamm_codegen が local partial を無音で欠落させる問題` — `{{#partialdef greet local}}...{{>greet}}` が VM では描画されるのに生成ヘッダから消えていた。dispatch に local も載せ同一経路で呼び出し
- `fix: injamm_codegen が bool/数値フィールドのセクションでコンパイル不能なコードを生成する問題` — スカラー値には range-for が成立しないため、`codegen_scalar_section_v` による `if constexpr` 分岐で VM と同じ「真なら 1 回」の if を生成。コンテナは従来どおり range-for（本体はセクションごとのラムダに 1 回だけ書き、両分岐から呼ぶ）
- `fix: injamm_codegen がセクション内の local partial をルート型で解決していた問題` — `{{#items}}{{#partialdef row local}}[{{n}}]{{/partialdef}}{{>row}}{{/items}}` がルート型に `n` を要求してコンパイル不能だった。ループ内の `local` partial には現在の要素を渡し、VM と同じ解決に（グローバル partial は従来どおりルート型）
- `fix: numify が文字列 "-0.0" を "-0" にしていた問題` — `-0.0` の符号ビットが `to_chars` の出力に残っていたため、0 を正規化（`codegen_helpers.hpp` の drift 実装も同期）
- `fix: injamm_codegen が {{@key}} / {{loop.key}} でコンパイル不能なヘッダを書き出す問題` — `data.@key` という不正メンバアクセスを生成したまま exit 0 だったのを、明確なエラーメッセージと非ゼロ終了で拒否
- `test: 上記の回帰テストを追加` — 変数右辺比較・逆セクションのルートフォールバック・数値セクション・join+break・numify/round の範囲外値・NTTP 未知フィルタ・同一キー+フィルタのパリティ（VM↔NTTP）、codegen の local partial / スカラー セクション / @key 拒否（badbc 拡張）

## 2026-11-18

- `fix: partial 本文のコンパイルエラーで custom_error_message が解放済みスタックを指す` — 子コンパイラの `error_ctx` を親へ伝播する際、`message` が partial 側 `template_storage` を指したままで、`extract_partials_impl` を抜けると未定義動作になっていた（ASan の stack-use-after-return で検出）。伝播時に親の `template_storage` 内の同一内容へ再束縛する
- `ci: sanitizer ジョブ（gcc/clang）を直列ビルドに` — ASan/UBSan では `tests/test_injamm.cpp` が 1 TU で 11–12GB 使うため、16GB ランナーの並列ビルドが OOM（exit 137）で落ちていた

## 2026-11-16

- `fix: -fno-exceptions で出力が推定容量を超えると切り詰められる問題を修正` — `hot_append` / `html_escape_append` の容量ガードが実行時にも働き、推定容量（リテラル×4＋var_ref×32）を超える出力が `out_of_memory` になるか、`emit_var_value` の戻り値が破棄されて成功扱いのまま切り詰められていた。実行時は `append` で伸長し、容量ガードは定数評価時のみに限定。長い変数・大量セクションの出力を回帰テスト化
- `fix: AOT 生成コードの if 条件を VM と一致` — `emit_if_or` / `emit_if_and` が右辺 (`operand3`) を無視していた問題と、`emit_if_filtered` が `"false"` / `"0"` を真と判定していた問題（反転分岐は死にコード）を修正
- `fix: injamm_codegen の .bc リーダを信頼境界として強化` — 巨大カウント/文字列長による abort・整数オーバーフロー・opcode/operand の範囲外アクセス・partial 再帰のスタックオーバーフローを、library loader と同じ上限（命令/リテラル/var_ref 1M、partial 1024、深さ 64、文字列 16MiB）と境界検証で拒否
- `fix: AOT ヘルパの LLONG_MIN 演算を VM と一致` — `filter_int_mod` / `filter_int_div` の `% -1` / `/ -1` で SIGFPE していた問題をガードし、`filter_int_bin` のバッファを 65 文字に拡大
- `fix: -fno-exceptions の不正フォーマットを abort ではなく invalid_format に` — `vformat` 前に書式指定子を検証
- `fix: HTML エスケープの single quote を AOT でも &#x27; に統一` — `--no-simd` 生成時のみ `&#39;` になっていたドリフトを解消
- `fix: バイトコード loader が literal_total_size を検証` — 改竄された巨大値による過大 reserve を拒否（stream/span 両パス）
- `fix: WASI ビルドで ctest が 0 件になる問題を修正` — `enable_testing()` を BUILD_TEST の外へ移動し、`BUILD_TEST=OFF` でも `no_exceptions_smoke` を実行可能に
- `fix: build.sh の POSIX 非準拠と STATIC フラグを修正` — `==` を `=` にし、STATIC に応じて vcpkg triplet を切り替え
- `chore: clang-tidy の readability-identifier-naming を有効化` — `Checks` 未登録で命名規約が未強制だった点と `portabilty-*` タイポを修正
- `test: 上記の回帰テストを追加` — no-exceptions の長い出力/不正 format/LLONG_MIN bin、AOT の論理条件・フィルタ条件・ヘルパ、壊れた .bc の拒否、fuzz に .bc loader を追加
- `docs: 例外なし・バイトコード・CMake オプションの記述を現状に同期` — 削除済み `ENABLE_WASI_MINIMAL` への参照、存在しない `injamm_bench_format`、fmt のプラットフォーム記述を修正
- `fix: partial 循環検出を全表記に拡張` — `{{>a}}`（空白なし）と `{{#partiallocal a}}` を検出対象に追加し、自己/相互参照を VM と同じくコンパイル時 `syntax_error`（"circular partial reference"）に
- `fix: NTTP の partial 循環・未定義参照をコンパイル時エラー化` — `render<Tmpl>` の partialdef 循環を consteval で検出し、外部レジストリ未定義の `{{> name}}` は `unknown_key`（名前付き）に。従来は実行時に空メッセージの `syntax_error` になっていた
- `fix: 未閉じ section / if の error position を開始タグ位置に` — 命令インデックスではなく `{{` のバイトオフセットを返す
- `fix: partialdef 抽出後も error position を元テンプレートと一致` — partialdef ブロックをテンプレートから除去せず `compile_body` が読み飛ばす方式に変更
- `fix: @var エラー位置を絶対オフセットに` — `undefined @var constant` / 循環エラーが元テンプレートの `@var(` 位置を指す
- `fix: 前処理後のエラー位置を元テンプレートへ逆変換` — コメント除去（`{{! }}` / `{# #}`）、タグ内空白除去、`{{#exists}}` 書換、`@var` 展開、partialdef 抽出の各変換にオフセットマップを持たせ、`error_ctx.position` が常に `bc_compile` 入力のバイトオフセットを指すように修正（partial 本文内のエラーも本文先頭オフセットを加算して変換）

## 2026-10-16

- `fix: バイトコード読み込みの信頼境界を強化` — loader の `is_simple` 整合性を検証し、`compare_operand_kind` / `section_filter_op_kind` / `string|int|float_filter` / `filter_flags` の値域検証を stream/span 両パスに追加
- `fix: 文字列フィルタのリソース増幅を抑制` — `repeat` を 10,000 回、`indent` 幅を 1,000 に制限
- `fix: `transform_exists_sections` の不正入力耐性を強化` — 空/複数トークンや空 stack を変換せず安全側に保持
- `docs: `invalid_utf8` を現行未使用として明記` — error_code/docs の契約ズレを整理
- `docs: 実行限界を README/SYNTAX に追記` — DoS 対策 caps を公開


## 2026-09-10

- `fix: 定数畳み込み if のスキップ経路が反転セクション {{^...}} を数えていなかった問題を修正` — `if 0` / `if 1` の到達不能節をテキスト走査でスキップする `skip_to_else_or_end` が depth を `#`・`/` のみで増減し `^` を無視していたため、`{{#if 0}}{{^items}}foo{{/items}}bar{{/if}}` のような有効なテンプレートが `syntax_error` でコンパイル不能になっていた（NTTP 側 `ct_parse` は `^` を追跡済みで、VM と挙動が食い違っていた）。skip 中も `^` を depth に算入して修正
- `fix: 閉じタグ名の不一致を構文エラー化` — ランタイムコンパイラが `{{/...}}` の名前を検証せず、`{{#user}}...{{/users}}` のような取り違えを黙って受理していた（`{{#flag}}A{{/items}}B` が `AB` を出力）。`compile_body_impl` に期待閉じ名を渡し、不一致は `syntax_error` に。NTTP パーサはキー一致で閉じタグを探すため、両エンジンの挙動が揃う
- `fix: 循環 partial をコンパイル時エラー化` — `{{#partialdef a}}{{#partial a}}{{/partialdef}}{{> a}}` のような自己/相互参照 partial がコンパイルを通過し、実行時に解決不能な `bc` を呼んで不可解な `syntax_error`（または無出力）になっていた。partial 依存グラフの DFS で back-edge を検出し `syntax_error` を返す（`{{#partial}}` / `{{> }}` 両形式）。`handle_call_partial` に partial 本体のコンパイルエラーを表面化するガードを追加し、`disassemble()` の未充填 partial エントリ参照も null 安全化

## 2026-09-08

- `feat: WASI 描画デモ injamm_wasi_render を追加し ctest を wasmtime 経由化` — bytecode→WASM 翻訳なしに VM ごと WASM 化できることの end-to-end 証明。`examples/wasi_render.cpp` は argv テンプレートを実行時コンパイルして固定デモデータ（section/if/filter/partial/escape）で描画する WASI 専用ターゲット（`BUILD_EXAMPLE=ON` かつ `CMAKE_SYSTEM_NAME=WASI` でのみビルド）。`CMakeLists.txt` は WASI 時に `CMAKE_CROSSCOMPILING_EMULATOR` を wasmtime に自動設定し `ctest --test-dir build-wasi` がそのまま通るように。代表 5 テンプレートで native/wasmtime のバイト一致を確認。README の wasip1 章に手順を追記（SYNTAX 変更なし）

## 2026-09-07

- `fix: wasip1/wasip2 で Catch2 が見つからず configure エラーになる問題を修正` — `vcpkg.json` が catch2 を `emscripten | !wasm32` に限定しているのに `CMakeLists.txt` が WASI でも `find_package(Catch2 CONFIG REQUIRED)` していたのが原因。WASI時は `injamm_tests` をスキップし Catch2 不要の `test_no_exceptions` のみビルドするようガード（`enable_testing()` は維持）。CI の wasip1/wasip2 ジョブと README の WASI 手順に `-DBUILD_TEST=OFF -DBUILD_EXAMPLE=OFF` を追加

## 2026-09-05

- `perf: mark cold OOM/format helpers gnu::cold to protect hot inline budget` — 粗粒度化後の wide 系 ~10% 残差を解析した結果、原因はホットパスの分岐ではなく TU 全体のインライン収支逼迫と判明（`--param=inline-unit-growth` 拡大で回復を確認）。`serialize_value.hpp` の cold 専用ヘルパ（`try_append_buf` / `oom_error` / `invalid_format_error` / `serialize_formatted`）に `[[gnu::cold]]` を付与し、マイクロベンチでベースラインに完全回復。全体でも総命令数 +0.7% で実質同等
- `feat: aggregate OOM handling at render boundary catch (coarse-guard redesign)` — append 毎の fallible 配管が描画系に 5〜15% の劣化を与えたため粗粒度ガードに再設計。ホット emit/serialize/filter を直接追記に戻し、OOM は `bc_execute` / `bc_execute_into` / `bc_execute_into_sink` の境界単一 `try/catch` で `out_of_memory` に集約（`invalid_format` のみ `expected` 伝播を維持）。`-fno-exceptions` 時の中間 OOM は trap に劣化（承認済み仕様）。境界は `bc_execute*` に置く（`injamm-sqlite3` が直接呼ぶため上位移動不可）。CI ゲートはホットパス try/catch 禁止に強化（`filters.hpp` はゼロ件を厳密検査）
- `docs: document no-runtime-exceptions guarantee` — 実行時パスは例外を送出せず OOM は `out_of_memory`（9）、不正フォーマットは `invalid_format`（10）を `expected` で返すことを README / SYNTAX のエラーコード表に追記。コンパイル時診断の `INJAMM_THROW` は維持。`test_noexcept` で検証、throw/try-catch の CI grep 検査あり

## 2026-09-02

- `fix: WASI minimal でも format / enum を有効化` — `std::format`/`fmt` は `-fno-exceptions` で `_GLIBCXX_THROW_OR_ABORT`/`assert_fail` にフォールバック、`enchantum` コア API (`to_string`/`cast`/`contains`) は `noexcept` のため例外なしでも動作する。`config.hpp` で `ENCHANTUM_THROW` を `injamm_trap` に差し替え（`INJAMM_NO_FMT`/`INJAMM_NO_ENUM_REGISTRY` は自動無効化しない）、`CMakeLists.txt` の `USE_FMT`/`ENABLE_ENUM` 自動無効化を撤廃、`vcpkg.json` の `enchantum` の `!wasm32` 制限を撤廃、`enum_io.hpp` に `config.hpp` インクルードを先行。README の WASI 制限表から `format`/`enum` 行を削除
- `ci: linux-wasi-minimal ジョブを wasi-sdk 34 + vcpkg `wasm32-wasip1` triplet でのビルドに移行` — `~/src/frozenchars` と同じ構成。`triplets/wasm32-wasip1.cmake`（chainload で `/opt/wasi-sdk` または `~/vm/wasi-sdk` を選択）を追加し `.gitignore` の `*.cmake` から除外。`vcpkg.json` の `catch2` を `emscripten | !wasm32` に限定（`enchantum` は WASI でも有効のため制限なし）。生成物が WebAssembly であることを `file` で検証
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
