# Changelog

## 2026-06-08

- `ab75ecc` initial commit
- `00af247` @root, @key を追加
- `3c9cc83` 色々加工用関数を追加

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

## 2026-06-10

- `8db6882` @var プリプロセッシングユーティリティを追加
- `2765cfd` @var が {{{}}}  raw タグ内でも展開されるように修正
- `0405fe6` ConstMap 対応の bc_compile オーバーロードと error_ctx フィールドを bytecode 構造体に追加
- `8be815f` engine&lt;T&gt; に map ライクなコンストラクタとエラーチェックを追加
- `9f54087` @var(name) 展開対応の NTTP render オーバーロードを追加

## 2026-06-11

- `7d4fdb8` ne, gt, gte, lt, lte 演算子を追加

## 2026-06-12

- `9c35a15` ct_bytecode, string_ref, builder, to_bytecode を追加
- `c0e798d` ct_chunks_to_bytecode を render&lt;&gt; に組み込み
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

## 2026-06-16

- `1b916b1` frozenchars がある場合、FrozenString を NTTP のテンプレート引数に指定可能に
- `b813420` @var の循環参照を防ぐ仕組みを追加

## 2026-06-17

- `e87fe6b` テンプレート内のコメントに対応
- `adb7620` @index1 に対応
- `ffe847b` trim_blocks / lstrip_blocks 機能を追加

## 2026-06-18

- `a222c7a` @size, コメントフォーマット追加、#exists、比較演算子、{{~~}}、replace フィルタを実装
- `b7ea3fc` @index/@first/@last/@key を loop.* にリネーム (inja 互換)
- `9db4755` パス探索の性能向上

## 2026-06-20

- `6465181` injamm-sqlite3: sqlite3_stmt を直接レンダリングする別ライブラリを追加
- `3165937` injamm-sqlite3: コードレビュー修正を適用
- `cc1a4fa` パフォーマンス改善提案を実装
- `d103336` 配列へのインデックスアクセスを可能に

## 2026-06-21

- `3ec3f54` コンパイル時の異常系処理対応
- `28d46af` 数値パースの高速化とフィルター種別の拡充
- `87aa502` clang が frozenchars で動かない問題への対応

## 2026-06-22

- `d1ca603` fmt を依存関係から削除

## 2026-06-24

- `6e742ea` コンテナを直接変数にバインドする injamm::bind を追加
- `904df7f` 数値フィルター用の四則演算を追加
- `4a5c109` upstream で追加したフィルターを injamm-ext-sqlite3 に追加
- `e034bdc` 長さを返す .size に対応

## 2026-06-25

- `81ba3ce` セクションに {{else}} を追加
- `7f5e3c5` 比較演算子・論理演算子・loop.parent.* を追加

## 2026-06-26

- `f39094a` engine のデフォルトコンストラクタを無効化
- `786e48b` オペコード探索の範囲外アクセスチェックを追加
- `81d648e` injamm::bind の引数なし命名を value から _ に変更
- `039de03` std::make_unique を外して微高速化
- `6a9918d` 文字列→整数変換のオーバーフロー対応
- `35a8f11` HTML エスケープ文字に対応
- `21464fa` 文字列内のエスケープ文字に対応

## 2026-06-27

- `a53eb5d` enum サポート

## 2026-06-30

- `28e2e15` injamm::sqlite3 に未実装だった内容を追加

## 2026-07-03

- `67d57b5` if の条件文が定数評価できる場合にスキップする最適化を追加
- `274ff8a` Partial Template ライクな機能を追加

## 2026-07-04

- `eaf5f7c` パフォーマンス改善: リザーブ見積もりの精度向上と静的ディスパッチ化 / 高速パス最適化とバッファ再利用 API の追加

## 2026-07-05

- `7afab40` clang でのコンパイルエラーを修正
