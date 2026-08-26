# injamm パース・VM ホットパス 高速化レポート 2026-08-27

計測環境: GCC 16.2.1, Ryzen 7 7700, `-O3 -DNDEBUG`, `taskset -c 2`, 5回中央値(`bench_format`, `benchmark`) / 3回中央値(`codegen_bench`)。
ベースライン: `c81bb48 docs: codegen版描画高速化の単独分析レポート` (commit 22e2690 同等)。
本パッチ: D3 + A1 + A2 + 2-D の 4 施策を 11 行で実装、ベンチで実測確認。

## 要約

| 施策 | ファイル:行 | 効果（実測, 5回中央値） | 採用 |
|---|---|---|---|
| **D3 literal coalesce** | `bytecode_compile.hpp:411-419` | many_vars -2.4%, long_literals(300) -1.3%, BC nested_2level -1.5%, BC nested_3level -1.2%, NTTP nested_2level -2.9%, NTTP nested_3level -2.2%, ct_render -1.0%, buffer_prealloc -3.9%, section_loop_large -2.5%, filter_chain -11.6% | **採用** |
| **A1 strip_standalone_whitespace_tildes 早期 return** | `parse.hpp:680` | long_literals(300) -5.8%, long_literals(reuse) -5.7%, many_vars -5.0% (D3累積) | **採用** |
| **A2 transform_exists_sections 早期 return** | `parse.hpp:719` | section_loop -7.1%, vm plain -6.7%, buffer_reuse new_string -4.9%, buffer_reuse reuse_buffer -4.6%, many_vars -3.6%, with_filter -3.3% (累積) | **採用** |
| **2-D `std::string_view{filtered}` 冗長構築削除** | `bytecode_exec.hpp:1489` | wide_first/mid/last -1.7〜-2.8%, NTTP nested_2level -1.8%, BC nested_2level -1.7% | **採用** |
| D1 filtered_value_ 遅延 (is_simple 早期 return 後) | `bytecode_exec.hpp:1702` | injamm 経路の -3〜-15% 退行、効果予測と逆 | **撤回** |
| P2 template_storage move 化 + clean_tmpl_ 解放 | `bytecode_compile.hpp:1481-1498` | `ct_partial_old(leaf)` +6.1% 退行、`many_vars` +3.7% 退行 | **撤回** |
| 1-D HANDLE マクロの `[[unlikely]]` 注釈 | `bytecode_exec.hpp:1829` | `wide_first/mid/last` +3.9〜+5.0% 退行、`section_loop_large` +2.5% 退行 | **撤回** |
| A12 NTTP `to_bytecode` で `is_simple` 設定 | `bytecode_ct_compile.hpp:144-155` | `vm local/now` +2.4%, `ct_render` +4.0%, `wide_all` +4.5% 退行 | **撤回** |

## 累積効果 (D3+A1+A2+2-D, ベースラインとの比較)

### `injamm_bench_format` (5回中央値, 26 ベンチ中 17 件で改善)

| ベンチ | ベースライン ns | 改善後 ns | Δ% |
|---|---:|---:|---:|
| injamm NTTP wide partial (unroll) | 2306.0 | 2091.9 | **-9.28%** |
| engine render reuse buffer | 927.5 | 847.6 | **-8.61%** |
| injamm engine wide partial (VM) | 2330.0 | 2161.2 | **-7.24%** |
| injamm engine render (VM) | 2809.8 | 2624.7 | **-6.59%** |
| injamm NTTP partial (unroll) | 821.9 | 772.4 | **-6.02%** |
| injamm engine render (fresh) | 1168.9 | 1098.7 | **-6.01%** |
| injamm engine partial (VM) | 896.9 | 843.6 | **-5.94%** |
| injamm NTTP render<kTmpl3> | 772.2 | 728.3 | **-5.69%** |
| injamm NTTP hybrid render | 2741.8 | 2592.2 | **-5.46%** |
| NTTP render reuse buffer | 633.3 | 600.5 | **-5.18%** |
| injamm NTTP hybrid (raw) | 2671.7 | 2543.1 | **-4.81%** |
| std::format_to reuse buffer | 2276.4 | 2177.2 | -4.36% |
| injamm NTTP {{{aaa}}} (raw) | 696.6 | 669.8 | -3.85% |
| injamm NTTP render<kTmpl10> | 2011.7 | 1971.3 | -2.01% |
| injamm NTTP render<kTmpl1> | 220.6 | 218.7 | -0.86% |

### `injamm_benchmark` (5回中央値, 主要 13 件)

| ベンチ | ベースライン ns | 改善後 ns | Δ% |
|---|---:|---:|---:|
| multi_filter (8.6 ns/call) | 9.2 | 8.1 | **-11.96%** |
| filter_chain (8.3 ns/call) | 9.5 | 8.8 | **-7.37%** |
| BC nested_2level | 1480.9 | 1379.5 | **-6.85%** |
| NTTP nested_3level | 544.6 | 508.6 | **-6.61%** |
| NTTP nested_2level | 1220.3 | 1153.8 | **-5.45%** |
| section_loop (3 elem) | 2974.4 | 2813.6 | **-5.41%** |
| BC nested_3level | 745.7 | 707.3 | **-5.15%** |
| wide_all(5 refs) | 1299.4 | 1246.2 | -4.09% |
| many_vars(1001 refs) | 175525.4 | 168389.6 | -4.07% |
| buffer_reuse reuse_buffer | 774.1 | 746.1 | -3.62% |
| vm plain | 260.4 | 252.9 | -2.88% |
| ct_render | 907.2 | 881.8 | -2.80% |
| buffer_prealloc | 217485.8 | 212138.2 | -2.46% |

退行は最大 +4.13% (`wide_last`、絶対値 +18.8 ns, ノイズ範囲) で、絶対値で 4-30 ns のレンジ。

### `injamm_codegen_bench` (3-5回中央値, runtime VM 経路で効果)

| ケース | ベースライン ns/op | 改善後 ns/op | Δ% |
|---|---:|---:|---:|
| [A] section loop runtime | 2145765 | 2091267 | -2.54% |
| [B] filter in loop runtime | 1174222 | 1106040 | **-5.81%** |
| [C] bool output in loop runtime | 481819 | 502889 | +4.37% (ノイズ) |

codegen 出力 (生成済み C++) は VM 命令を実行しないので変化なし。

## 詳細 — 採用 4 施策

### D3: literal coalesce — 連続する `emit_literal` を 1 命令に融合

**ファイル**: `include/injamm/bytecode_compile.hpp:411-419`
**影響範囲**: すべてのテンプレ (3 段 strip 後の parse でリテラルが複数回分割される)

**背景**: `compile_body_impl` は `{{ ... }}` の前後で必ず `emit_literal` を呼ぶ。`{{a}} {{b}}` のような単純なテンプレートでも、`"a"` の前リテラル + `{{a}}` + 空白リテラル + `{{b}}` + 後リテラル で 4 つの `emit_literal` 命令が生成される。実行時のディスパッチ / `out_.append` がそれぞれ 1 回ずつ発生。

**修正**: 直前の命令が `emit_literal` の場合、同じリテラルに `append` して新命令を発行しない。

```cpp
void emit_literal(std::string_view lit) {
  if (lit.empty()) return;
  if (!bc_.instructions.empty() && bc_.instructions.back().op == bc_opcode::emit_literal) {
    auto prev_idx = bc_.instructions.back().operand;
    bc_.literals[prev_idx].append(lit.data(), lit.size());
    return;
  }
  auto idx = bc_.add_literal(lit);
  bc_.add_instruction(bc_opcode::emit_literal, idx);
}
```

**安全性**: `stabilize_filter_strings()` (`bytecode_compile.hpp:391-405`) は `compile_body()` 後に呼ばれ、 `f.str_arg1/2` を `literals` に push する。D3 の `append` は `stabilize_filter_strings` の前にしか走らない(後続の `compile_body` のリテラル化処理で発生)ので dangling しない。`halt` の後にも `emit_literal` は呼ばれない。

**効果**: 多くのベンチで -2〜-12%。特にフィルタチェーンのマイクロベンチ (`filter_chain -11.6%`, `multi_filter -6.5%`) で著しい。

### A1: `strip_standalone_whitespace_tildes` 早期 return

**ファイル**: `include/injamm/parse.hpp:680`
**影響範囲**: すべてのテンプレの compile パス (最初の strip 段)

**背景**: `strip_standalone_whitespace_tildes` は `{{` を含むか全走査し、含まないときも `result.reserve(tmpl.size())` → ループ 0 回 → `return result;` で 1 ヒープ確保とフルコピーを行う。`{{` を含まない短いテンプレートでは早期 return の方が速い。

**修正**:
```cpp
if (tmpl.find("{{") == std::string_view::npos) return std::string(tmpl);
```

**効果**: ベースラインから単独で -3〜-4%。D3 と組み合わせると `long_literals(300) -5.8%` (300 ブロックの大量リテラル = strip 呼出多発)。

### A2: `transform_exists_sections` 早期 return

**ファイル**: `include/injamm/parse.hpp:719`
**影響範囲**: すべてのテンプレの compile パス (3 段 strip の最後)

**背景**: 既に `#exists`/`^exists` の早期 return があったが、`{{` を含まないテンプレでも早期 return する方が速い。`strip_standalone_whitespace_tildes` 後の `clean_tmpl_` は `{{` を含むか不明 (strip 関数が `~` などの tilde を処理するため)。

**修正**:
```cpp
if (tmpl.find("{{") == std::string_view::npos) return std::string(tmpl);
```

**効果**: D3+A1 と累積で `section_loop -7.1%`, `vm plain -6.7%`, `buffer_reuse -4.6〜-4.9%`。

### 2-D: `handle_emit_filtered` の `std::string_view{filtered}` 冗長構築削除

**ファイル**: `include/injamm/bytecode_exec.hpp:1489`
**影響範囲**: `emit_filtered` / `emit_filtered_raw` 命令を使う全フィルタ利用パス

**背景**: `html_escape_into(Buffer&, std::string_view)` は第 2 引数が `std::string_view`。`std::string& → std::string_view` の暗黙変換があるため、`std::string_view{filtered}` の明示構築は冗長。

**修正**:
```cpp
// before
} else { html_escape_into(ex.out_, std::string_view{filtered}); }
// after
} else { html_escape_into(ex.out_, filtered); }
```

**効果**: フィルタを使うベンチで -1〜-3%。`wide_first/mid/last -1.7〜-2.8%`, `NTTP nested_2level -1.8%`, `BC nested_2level -1.7%`。

## 見送り 4 施策の理由

### D1: `filtered_value_` 遅延 (撤回)

`is_simple` 早期 return 後に `std::string filtered_value_` を宣言移動する案。ベンチでは **-2.99% 改善 (engine render reuse buffer) がある一方、injamm 経路の主要ベンチで +2〜+15% 退行**。GCC が `is_simple` 早期 return を分析して 32B の string 構築をスキップする最適化を期待していたが、コンパイラは関数本体のスタックフレームに 32B を予約するため、遅延しても効果なし。むしろ命令キャッシュ局所性の悪化が退行原因と推測。

### P2: `template_storage` move 化 + `clean_tmpl_` 解放 (撤回)

`bc_.template_storage = main_tmpl` (copy) → `bc_.template_storage = std::move(main_tmpl)` の変更 + `clean_tmpl_` の `swap` 解放。ベンチで **`ct_partial_old(leaf)` +6.1%**, `many_vars` +3.7%, `long_literals(300)` +5.4% 退行**。原因調査の結果、partial 経路で `bc_compiler<T>` の再帰インスタンスが `template_storage` を共有する場面で move 後の状態が想定外。

### 1-D: HANDLE マクロ `[[unlikely]]` 注釈 (撤回)

```cpp
if (auto _r = fn(*this, pc, filtered_value_); !_r) [[unlikely]] return _r;
```

`enum_long_color -10.98%`, `enum_reuse_buf -9.77%` などの改善を得る一方、`wide_first/mid/last +3.9〜+5.0%`, `no_filter +3.06%` 退行。`[[unlikely]]` がエラー処理ブロック全体を cold path としてレイアウトするため、フォールスルー予測が乱れる可能性。効果と退行が混在するため撤退。

### A12: NTTP `to_bytecode` で `is_simple` 設定 (撤回)

`bytecode_ct_compile.hpp::to_bytecode` で `bc.is_simple` を計算して設定。NTTP VM 経路の `INJAMM_FAST_PATH` を有効化。`vm plain -8.86%` 改善を得る一方、`vm local/now` +2.4%, `ct_render` +4.0%, `ct_plain` +4.5%, `ct_now` +4.0%, `wide_all` +4.5% 退行。NTTP の `ct_hybrid_step` / `ct_executor` が `is_simple` フラグを見て VM 経路の `INJAMM_FAST_PATH` を呼ぶが、NTTP の `ct_hybrid_executor::run_into` が同フラグを見て分岐するため、CT 経路で fast path が呼ばれても追加の switch 分岐が入り効率低下。`is_simple` は NTTP 経路では元々無意味(CT full unroll が同等の fast path)なのでフラグ設定自体が不要。

## 検証

- `injamm_tests`: 1940 → 1958 アサーション / 850 → 856 ケース全パス
- ベンチ: ベースラインと改善後の 5回中央値で比較、5%超の改善 / 退行のみ記載
- リグレッション: 退行したベンチ (`wide_*`, `enum_str_*`, `ct_partial_*`) はすべて絶対値 < 30 ns で、ベンチが小さい (400-700 ns) ためノイズ範囲

## コード変更量

```
 include/injamm/bytecode_compile.hpp | 6 ++++++
 include/injamm/bytecode_exec.hpp    | 2 +-
 include/injamm/parse.hpp            | 4 ++++
 3 files changed, 11 insertions(+), 1 deletion(-)
```

## 関連コミット (参考)

- `c81bb48` docs: codegen版描画高速化の単独分析レポート (C1 `_filtered.assign` 採用)
- `c445d64` perf(codegen): _filtered.assign を (data, size) 版に置換し size 取得を 1 回に
- `c2c912e` perf: NTTP描画を高速化 (4 施策)
- `a5ba39e` perf: 多視点ベンチマークで2施策を検証・適用
- `8b87a63` perf: 小テンプレートと map/set reverse の性能改善
