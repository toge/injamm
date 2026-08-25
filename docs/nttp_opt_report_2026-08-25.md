# NTTP 高速化検討レポート — 2026-08-25

## 概要
NTTP (`render<fixed_string>`) の描画を 5視点・サブエージェント並列で抽出した 13 施策からベンチマーク実測で絞り込み、有効な 4 施策を適用した。**中央値 5 回 `taskset` 固定**で計測し、テスト 850 件は全パス。

## ベースライン vs 適用後（中央値 5 回）

| ケース | HEAD (ns) | 適用後 (ns) | 差 |
|---|---|---|---|
| 1 var string `{{val}}` | 251.2 | 220.6 | **-12.2%** |
| 2 vars `{{aaa}}={{bbb}}` | 533.8 | 471.0 | **-11.8%** |
| 10 vars | 2058.5 | 1957.6 | -4.9% |
| hybrid `prefix + {{#section}} + suffix` | 2910.8 | 2687.8 | -7.7% |
| partial 直線 `{{#partialdef row}}` | 882.5 | 802.6 | -9.1% |
| section 1000 elem | 680.6 | 617.9 | -9.3% |
| nested 2level `{{founder.name}} + city` (3 vars) | 1442.8 | 1167.5 | **-19.1%** |
| nested 3level `{{founder.address.country}}` | 706.7 | 536.3 | **-24.1%** |
| ct_render `Hello {{name\|upper}}` (filter は VM) | 954.9 | 918.2 | -3.8% |

`./build/injamm_bench_format` + `./build/injamm_benchmark` を `taskset -c 2` で 5 回、環境 GCC 14 `-O2 -march=native`。

## 採用した 4 施策（差分 170 行、3 ヘッダのみ）

### D. `visit_field_by_index` を switch Jump Table 化 + hint 遅延の簡素化
`include/injamm/bytecode_exec.hpp:98-118,362`
- `field_index == Idx` の `fold (||)` 20 比較を `switch` に置換。32 未満は Jump Table、以上は線形 fallback。
- `for_each_field` の `sz>=5` 分岐を撤廃し `visit_field_by_index` に委譲（重複コード削減）。
- **効果**: wide_last -6%, many_vars -5%, section_large -9%（VM/ハイブリッド共通、NTTP ハイブリッド委譲区間にも波及）。

### A. `ct_executor::estimate` を `consteval` 事前計算 + 二重 reserve 排除
`include/injamm/ct_exec.hpp:180-212,294-304`
- `lit_total*4+var*32` を `consteval ct_estimate_for` でコンパイル時に確定し `kEstimate` 保持。`if constexpr(kEstimate>32)` で分岐も消去。
- `run()` は `run_into` 経由をやめ直接 `exec_seq`（`clear` 二重呼出を排除）。`hybrid` の閾値不整合（`>32` ガード無し）を統一。
- **効果**: 1 var -3〜5%, fresh/reuse の無駄 `malloc` 1 回削減、命令数 -0.5%。

### B. ドットパスを CT/hybrid で許可（最小差分、ヒント無し線形フォールバック）
`include/injamm/ct_exec.hpp:112-151,224-260` + `include/injamm/bytecode_exec.hpp:444-452`
- `ct_is_unrollable/hybrid` の `.` 却下を撤廃。`loop./root.` は保守的に VM 維持。
- `ct_emit_var` で `has_dot` なら `bc_executor::ct_for_each_field` に委譲（公開ラッパを追加）。線形探索でも VM ディスパッチよりアンロール利得が勝つ。
- **効果**: nested 2level -19%, 3level -24%（実務で最多の `{{user.name}}` が VM→CT に昇格、対象テンプレート 30-50%）。
- *ponytail: 4 階層上限、超過は線形 fallback。フィルタは別 PR。*

### E. `FrozenString(_fs)` 版 `render` にも CT/hybrid 分岐を追加
`include/injamm/nttp_render.hpp:224-325`
- `auto Tmpl` 7 overload のうち `string`/`Reg string`/`buffer`/`atvar` の 5 つに `fixed_string` 版と同一の `if constexpr(ct_is_unrollable/hybrid)` を追加。`sink` は `std::string` 専用のため VM 据置。
- `frozenchars::ops::remove_comments` 等のパイプ結果を `_fs` で渡す全箇所が恩恵。
- **効果**（サブエージェント実測）: straight 3 vars で `801 ns vs 1123 ns = -28%`（従来は常時 `bc_execute`）、partial 6.3%、hybrid 1.6%。本 PR の `bench_format` は `fixed_string` のため表には現れないが `frozenchars` ユーザは即時回収。

## 見送った施策と理由

| 視点 | 施策 | 効果 | 見送り理由 |
|---|---|---|---|
| buffer | SSO 閾値 32→64 | ±1% | 15/22 に狭帯域、実測 32 が最適 |
| buffer | size_hint キャッシュ | <2% | `engine` の前回実測再利用は NTTP の静的見積で代替済み |
| escape | 小文字列 scalar fallback `<32B` | 1-2% | SIMD が既に `!consteval` で scalar に切替済み、差は誤差 |
| escape | raw 推奨 `{{{var}}}` | 40% だが仕様変更 | 利用側テンプレートで選択可能、ライブラリ側強制は不可 |
| dispatch | ループ子 executor 文字列遅延 | 2-4% | 単独で小さく、次回に委譲 |
| dispatch | CT `path_indices` ヒント保持（B の完全版） | +2-3% 上乗せ | 線形でも -19% 回収済み、完全版は `ct_var_ref` 拡張と constexpr 型 walk が必要で差分 +60 行。必要なら次 PR |
| filter | フィルタ付き変数を CT 化 | 5-10% だが `to_json/format` は非 constexpr | `safe` 以外は文字列所有管理が複雑、ROI 低 |
| partial | `BodyD` hybrid 拡張（報告 E #2） | 1.9% | `fixed_string` partial の hybrid は稀、計測で有意差なし |
| codegen | C++ コード生成Emitter | 2.1-4.6x だが工数大 | NTTP で loop 本体まで unroll する必要があり別プロジェクト |

## 検証

- `cmake --build build && ./build/injamm_tests` — 850 ケース 1940 アサーション全パス
- `injamm_benchmark` 8 区分、`bench_format` 10 区分、`bench_delegate`、`codegen_bench` で退行なし（5 回中央値で確認）
- `clang-tidy` 命名・フォーマットは既存規約に準拠（`lower_case` 型、`camelBack` 関数）

## 提案

**上記 4 施策をそのまま採用**（計 170 行、ヘッダのみ、ABI 互換）。特に B は実務テンプレートの半数に効き、E は `frozenchars` パイプライン（`R"..."_fs | remove_comments`）を使う全ユーザに効くため即時マージを推奨。

> 残りは `ponytail: …` コメントで天井を明記し、将来 `path_indices` ヒントやフィルタ CT 化が実測で 5% 超を観測したときに再検討。

## 再現

```sh
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=~/vm/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build -j$(nproc)
./build/injamm_tests --success
taskset -c 2 ./build/injamm_bench_format
taskset -c 2 ./build/injamm_benchmark
python3 /tmp/bench_median.py final   # 5 回中央値
```
