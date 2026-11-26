# Issue #11: injamm_codegen パリティ修正 設計

日付: 2026-11-25

## 目的

`injamm_codegen` の AOT 生成コードを、VM (`engine<T>`) と `render<fixed_string>` が既に実現している挙動へ一致させる。対象は Issue #11 本文の全項目と、Issue コメントで追加された文字列セクションの文字単位反復の不一致である。

この変更は公開テンプレート構文を追加せず、既存構文が AOT でも同じ結果になることを保証する。

## 対象

- セクションおよび逆セクションのラムダ本文内にある `{{#break}}` / `{{#continue}}`
- セクションフィルタ、インデックス依存、非フィルタ経路を含むループ機械全体の値 → ルート二段解決
- 単独、否定、`&&`、`||`、フィルタ済み、比較系の `{{#if}}` における値 → ルート二段解決
- 整数リテラル、文字列リテラル、変数参照、ループ内 `this` を比較 RHS とする `{{#if}}`
- 比較 RHS が未対応または不正なとき、黙って `0` と比較する経路の除去
- `std::string`、`std::string_view`、文字列ポインタを「非空なら現在スコープで 1 回実行するスカラーセクション」として扱う
- 生成 → C++ コンパイル → 実行 → VM 比較のライブテスト
- `README.md`、`SYNTAX.md`、`CHANGELOG.md` の影響確認と必要な更新

## 非目標

- バイトコード形式、VM、NTTP の実行モデル変更
- AOT 生成コードを VM の状態機械へ置き換えること
- `{{loop.key}}` など、Issue #11 外の未対応構文を新規対応すること
- VM の比較規則や特殊変数 RHS を拡張すること
- テンプレート構文や公開 API の追加

## 設計方針

### 1. 既存バイトコード情報を利用する

バイトコード形式は変更しない。AOT 生成器は既にシリアライズされている次の情報を利用する。

- `compare_rhs_kind`
- `compare_rhs_text`
- `root_fallback`
- セクションフィルタパイプライン
- LHS/RHS の変数参照

`root_fallback` は既存の C++ 型情報を参照するためのメタデータとして扱い、AOT 内で Glaze の実行時キー探索へ変換しない。これにより生成コードの直接フィールドアクセスを維持する。

### 2. 二段解決をキー別関数テンプレートへ統一する

値スコープ相対のキーは既存の `cg_get_*` 関数テンプレートを通して解決する。ヘルパは次の形を維持する。

```cpp
template <class V, class R>
inline decltype(auto) cg_get_key(const V& value, const R& root) {
  if constexpr (requires { value.key; }) {
    return (value.key);
  }

  return (root.key);
}
```

実際にはアクセサを一方の式として扱い、値 → ルートの選択をヘルパの関数テンプレート内に閉じ込める。これは Clang / MSVC におけるラムダの `if constexpr` 破棄側の早期検証を避け、既存の #10 修正方針と一貫する。

補間だけでなく、次の出力箇所を同じアクセス式生成経路へ通す。

- セクション・逆セクション対象
- フィルタセクションの `size()`、境界、要素アクセス
- `if` の単独、否定、論理演算
- 比較 LHS
- 比較変数 RHS
- フィルタ済み `if` の元値
- `emit_var_size`

`this`、`loop.*`、`loop.parent.*` はメンバアクセサではなく生成器が専用式へ変換する。特に比較式の `this` は `_itemN` 自体へ解決し、`_itemN.this` は生成しない。

### 3. ループ制御は必要なセクションだけフラグを用いる

事前走査で、各制御命令の対象となる VM ループ状態を記録する。制御命令を含まないテンプレートでは既存の出力形状とホットパスを維持する。

制御命令がある場合の規則は以下とする。

- ラムダ本文の `emit_break` は `_brkN = true; return;`
- ラムダ本文の `emit_continue` は `_continueN = true; return;`
- 実コンテナループは各反復前にフラグを初期化する
- 本文後に continue フラグを先に処理し、続けて break フラグを処理する
- `join` 区切り文字は本文の成功後、break / continue の判定前ではなく、制御フラグの確認後に出力する
- bool / 数値 / enum / optional セクションと逆セクションは独立ループ状態を作らない。制御命令は親ループのフラグへ伝播する
- 文字列セクションは VM と同じ専用 `guard` を持つ。break / continue は文字列本文を `return` で停止するが、外側コンテナループのフラグへ伝播しない
- トップレベルで制御命令だけが実行されても不正な C++ を生成せず、VM と同じく出力停止以外の効果を持たない

条件分岐やネストしたセクションに制御命令がある場合は、VM のループ状態と同じ対象へ状態を返す。単にラムダのある最内ブロックでフラグを消費しない。文字列セクションをまたぐ場合だけはガードが伝播境界になる。

### 4. 文字列セクションは文字を反復しない

`codegen_scalar_section_v` またはこれと同等の共有判定に、次を含める。

- `std::string`
- `std::string_view`
- `const char*`
- `const char[N]` から経由した文字列ポインタ型

非空文字列は、現在のスコープを変えず本文を 1 回だけ実行する。空文字列は本文を実行せず、`else` があれば `else` を実行する。

生成コードは `char` ごとの range-for を出してはならない。

この分類は通常 range-for、インデックス必須ループ、セクションフィルタ付きループの選択前に行う。`section_op_count > 0` や `index_loops_` にあることを理由に文字列へ `.size()` / 添字アクセス / range-for を先に出力しない。非空文字列では VM と同じく文字数ではなく本文を 1 回だけ実行し、`loop.index` / `loop.is_first` 等が必要な場合も専用 guard の既定状態へ合わせる。

### 5. 比較式は RHS 種別を保持する

`get_compare_value` の「整数フィルタの先頭、なければ `0`」という処理を比較式生成の基準から廃止する。

RHS は `compare_rhs_kind` に応じて生成する。

| RHS 種別 | 生成 |
| --- | --- |
| 整数リテラル | 整数値 |
| 文字列リテラル | C++ 文字列リテラル |
| 変数参照 | LHS と同じ二段解決規則で値式を生成 |

空文字列リテラルは `compare_rhs_kind == string_literal` かつ `compare_rhs_text.empty()` として正しく表現される有効な入力なので、受け入れる。

不明な比較種別、整数リテラルの値がないバイトコード、空キーの変数参照は、不正なヘッダを書き出さず codegen を失敗させる。既存 reader が対応する旧バイトコードの整数値表現は維持し、新しいバイトコード形式は導入しない。

`this`、`loop.*`、`loop.parent.*` の専用解決は比較 LHS に適用する。現在のコンパイラ/VM はこれらを特別な比較 RHS として分類しないため、RHS 側へ同等機能を追加しない。LHS `this` は現在ループ値、数値 LHS の `loop.index` / `loop.index1` / `loop.size` は VM の現在値へ解決する。

### 6. VM と同じ型別比較規則を使う

直接演算子だけへ置換せず、AOT 共有ヘルパで VM の型別規則を実装する。

- 整数と浮動小数点は VM と同じ数値昇格規則で比較する
- enum は underlying 型へ変換する
- optional RHS は値ありの場合だけ内部値を比較対象にし、値なしは偽とする。optional LHS は現行 VM が比較処理を持たないため偽を返す
- 文字列対文字列は `==` / `!=` を評価し、順序比較は VM と同じく偽とする
- 数値 RHS と数値文字列 LHS は VM と同じ整数変換を試みる
- 数値 LHS と文字列 RHS は VM と同じく数値へ変換せず、文字列リテラル / 変数右辺が数値変換済みでなければ条件を偽とする
- 未解決、値なし、変換不能の比較は VM と同じく偽とする

比較ヘルパは `codegen_helpers.hpp` に置き、生成コード本体では共通呼び出しとして使用する。ヘルパ自身は C++23 で記述し、MSVC / Clang / GCC の型推論に依存しない分岐にする。

## 生成器内部の構成

`util/injamm_codegen.cpp` に次の責務を明確にする。

1. **事前解析**: ブロックスタックを使い、各ループのインデックス参照、二段解決キー、制御帰属、型別比較要否を収集する。
2. **式生成**: 値式、ルート式、特殊変数式、比較式を同じヘルパ群から組み立てる。
3. **セクション分類**: optional / scalar / string / container の判定を通常、インデックス、フィルタ付きの全ループ経路より前に行う。
4. **命令生成**: 命令種別に応じた文だけを出力し、アクセス規則や比較規則を各 opcode 実装へ重複させない。
5. **ブロック終了**: ループ、ラムダセクション、条件ブロックの終了処理で制御フラグを消費または親へ伝播し、文字列 guard を伝播境界として扱う。

`open_block` には制御帰属に必要なフラグ、反復深度、ルート引数、`else` 状態を保持する。既存の `loop_depth_` とラムダ連番を不用意に増やさず、セクションのスコープ変更規則を維持する。

## エラー処理

- 不正な変数参照インデックス、比較種別、必要な RHS 情報の欠落は、ヘッダ出力前に `code_generator::generate()` が失敗させる
- 生成コードは未知キーや型不一致を `0`、空文字、`false` などへ無音フォールバックしない
- VM が「未解決値を偽とする」場合の挙動と、AOT 生成器が入力を受け入れられない場合を分離する
- `render` / `render_into` の戻り値契約は変更しない

## テスト

### ライブパリティ

`test_codegen_live` に次を追加する。各ケースは生成ヘッダをコンパイルし、少なくとも truthy と falsy の 2 データセットで VM と比較する。

- コンテナループ直下の break / continue
- セクション本体内の break / continue
- 逆セクション本体内の break / continue
- `if` 内にある break / continue
- `join` / `take` / `reverse` / `stride` を含む反復での制御。区切り文字は成功した直前本文の後だけに出力されること
- ループ内、非存在要素キーからルートへのループセクション fallback
- インデックス必須ループ内 fallback
- `if` 単独、否定、AND、OR、filtered の fallback
- 数値・文字列・enum LHS、optional RHS を含む比較
- 数値変数 RHS、文字列変数 RHS、非空 / 空の文字列リテラル RHS
- LHS `this`、`loop.index`、`loop.index1`、`loop.size`
- 浮動小数 LHS と整数 / 浮動小数 RHS、数値文字列 LHS と変換不能値
- LHS と RHS の双方にルート fallback が必要な比較
- 非空・空の string / string_view / 文字列ポインタセクション
- `take` / `reverse` / `loop.*` を含む文字列セクション
- ループ内文字列セクションと break / continue の guard 境界
- ループ内文字列セクションの `this`

具体的な CMake ケースとして、少なくとも `loop_break`, `section_break`, `inverted_continue`, `nested_control`, `join_break`, `loop_filter_root_fallback`, `index_loop_root_fallback`, `if_root_fallback`, `if_compare_variable`, `if_compare_string`, `if_compare_empty`, `if_compare_this`, `string_section`, `filtered_string_section` を生成・コンパイル対象にする。`CaseData` にはテンプレートごとに root-only / value-only のキー、数値ペア、文字列ペア、`tags` コンテナ、空文字列、optional RHS が存在するよう拡張する。

### 静的・互換性

- `codegen_badbc_test` に不正な比較 RHS 種別・欠落データの拒否を追加する
- 通常生成ヘッダと `--no-simd` 生成ヘッダの差異が意図した範囲に収まることを確認する
- 同梱 `render*.hpp` は必要なものだけ `regenerate.sh` で再生成する
- GCC を必須検証とし、利用可能な Clang でも `test_codegen_live` を構築する
- MSVC では既存の関数テンプレート二段解決を維持し、型依存しないメンバアクセスをラムダ外側へ漏らさない

## ドキュメント

公開 API や構文を追加しないが、AOT の対応範囲と挙動を修正するため、以下を必ず確認する。

- `README.md`: AOT コード生成の対応状況や既存セクション / `if` の説明に誤りがあれば更新
- `SYNTAX.md`: `if` 比較、break / continue、文字列セクションの説明と出力例を実装へ一致させる
- `CHANGELOG.md`: Issue #11 の修正を日付降順で追記する

構文一覧・フィルタ表を `parse.hpp`、`bytecode_fwd.hpp`、`filters.hpp`、`bytecode_compile.hpp` と照合し、無関係な構文変更はしない。

## リスクと緩和策

- **ラムダ間の制御伝播漏れ**: 事前解析で制御所属を検証し、ネストケースをライブテストへ追加する。
- **Clang / MSVC で破棄側が実体化される**: メンバ存在判定を `cg_get_*` 関数テンプレートへ閉じ込める。
- **比較ヘルパが生成コードを過剰に重くする**: `if constexpr` で型別コードへ削減し、出力ホットパスは現在の直接式を可能な限り維持する。
- **文字列を iterable として扱う回帰**: optional、scalar、container の判定順を明示し、string 系を container より先に判定する。
- **古い bytecode 互換性の破壊**: 新形式を追加せず、既存 reader の整数比較表現を維持する。
- **join と制御フラグの順序差**: 本文後・フラグ確認後の区切り出力を VM に対するライブテストで検証する。

## 完了条件

- Issue #11 の全再現テンプレートとコメント追加の文字列セクションが VM と同じ出力になる
- `{{#if x == y}}` を含む静的にコンパイル可能なテンプレートで、黙った `0` 比較が残らない
- 生成された C++ が GCC / Clang でコンパイル・実行できる
- `test_codegen_live`、関連 codegen テスト、既存テストスイートが通る
- README / SYNTAX / CHANGELOG の確認または更新が完了する
