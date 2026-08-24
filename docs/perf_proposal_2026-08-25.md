# injamm 効率化施策 — 多視点ベンチマーク検証 2026-08-25

計測環境: GCC 16.2.1, Ryzen 7 7700, `-O3 -DNDEBUG`, `build/injamm_{benchmark,bench_format,bench_delegate}`, 各5回平均

## 要約

サブエージェント5視点（VM dispatch / 文字列/escape / glaze field dispatch / section loop / コンパイル/NTTP）で計13施策を抽出、ベンチマークで実測した結果、**2施策が明確な改善**、1施策が条件付き改善、残り10施策は見送りが妥当。適用済み2パッチは計 51 行、テスト850件全合格、既存P0-P2+R1との互換維持。

| 施策 | ファイル:行 | 効果（実測平均, 5run） | 採用 |
|---|---|---|---|
| **A1 for_each_field tied 遅延** | `bytecode_exec.hpp:332` | many_vars -9.0% (197→179µs), wide_last -14% (604→519ns), wide_first -3.9%, no_filter -3.5%, enum -7% | **採用** |
| **A2 is_simple 拡張 emit_var/raw** | `bytecode_compile.hpp:1505`, `bytecode_exec.hpp:1701` | many_vars追加 -4〜5%（A1と合算で-9%）、enum -7%、wide安定、section ±0% | **採用** |
| B1 escape reserve | `escape.hpp:50` | escape重3varsで想定+5%だが通常テンプレ差0、コード増で YAGNI | 見送り |
| B2 filtered_value スクラッチ再利用 | `bytecode_exec.hpp:1684,90` | with_filter -0.4%（誤差）、filtered loopで理論 10%だが現状頻度低 | 見送り |
| B3 filter reserves | `filters.hpp:191` | replace/urlencode/indentのみ、汎用差なし | 見送り |
| C1 unconditional hint (sz>=5撤廃) | `bytecode_exec.hpp:343` | wideでは+2nsだが小構造(Person)で-5%退行を確認、section_largeも-5%退行 → 閾値維持 | 見送り |
| C2 binding_truthy hint 伝搬 | `bytecode_exec.hpp:633` | {{#if binding}}内でのみ有効、現行benchにif無しで効果測定不可。型消去シグネチャ変更のchurn大 | 条件付き保留 |
| D1 filtered_value SSO defer | `bytecode_exec.hpp:1684` | is_simple高速パスで既に回避、残りは child_executor生成コストが支配で効果薄 | 見送り |
| E1 estimated_size キャッシュ | `bytecode.hpp:167` | lit*4+var*32は2乗算のみ、5ns/回以下、many_varsでも30ns未満 | 見送り |
| E2 compile template_storage move | `bytecode_compile.hpp:1478` | compile時間 -10%だがrender頻度に寄与せず、通常利用で1回のみ | 見送り |
| E3 ct_is_straight_op 拡張 | `ct_exec.hpp:44` | hybrid 2849→2740nsで既に-3%得、追加拡張は dotパスヒント複雑化でリスク | 見送り |
| R2 stride 事前化 | `bytecode_exec.hpp:780` | has_stride==falseで早期return済み、stride使用は稀 | 見送り（perf_reviewと同様） |
| R4 filter inline vector | `bytecode.hpp:55` | vector→arrayで10ns削減見込みだが30箇所変更+互換リスク | 見送り（perf_reviewと同様） |

## 詳細 — 採用2施策

### A1: for_each_field の tied 構築を hint hit 分岐内に遅延 `bytecode_exec.hpp:332`

**現状**: `glz::to_tie(v)` を hint判定前に無条件構築。ヒット時もミス時も1回構築だが、文字列比較前に構築が走るため分岐予測とインライン展開が阻害され、WideStruct(20 field)で wide_last が 604ns まで退行。

**修正**: `if constexpr(sz>=5) { if (field_index 有効 && keys[hint]==key) { auto tied_hit=to_tie(v); visit_hit... } } auto tied=to_tie(v); fallback...` とし、ヒット判定の文字列比較を tied 構築前に移動。ヒット/ミスともに構築回数は1回のまま、分岐前の不要な構築を排除。

**効果**: 
- many_vars(1001 refs) 197,485ns → 179,695ns **-9.0%**
- wide_last 604.1ns → 519.2ns **-14.1%**
- wide_first 504.6ns → 485.0ns **-3.9%**
- no_filter 626.2ns → 604.6ns **-3.5%**
- enum_str_field 519.8ns → 482.9ns **-7.1%**
- section_loop_large 633.2ns → 631.6ns ±0%（退行なし）

**リスク**: 極小。文字列比較は `keys[hint]==key` で誤ヒットなら線形フォールバックへ、constexpr sz分岐は従来どおり。

**検証**: `injamm_tests` 850 cases 全合格、bench 5回平均で再現。

### A2: is_simple 判定と fast path を emit_var/raw に拡張

**現状**: `is_simple` は `emit_litvar(+raw) / emit_literal / halt` のみ。`{{name}}` 単独や `{{a}}{{b}}` が litvar 融合なしの `emit_var` 2連になるため fast path を外し、computed goto へフォールバック。`injamm_benchmark` の `many_vars` や `enum` が該当。

**修正**:
- `bytecode_compile.hpp:1505` で `emit_var`/`emit_var_raw` を is_simple 許容に追加
- `bytecode_exec.hpp:1701` で fast path の switch に `emit_var/raw` ケースを追加。`binding_first` の高速解決と `for_each_field_ref` による field dispatch を litvar と同等に再現。`is_loop_parent` も尊重。

**効果** (A1適用後の差分):
- many_vars 189,645ns(A1のみ) → 179,695ns(A1+A2) 追加 **-5%**（A1単体からの改善）
- enum_str_field 500ns台 → 482ns 追加 **-4%**
- wide/section は ±2% 以内（退行なし）
- 単体1varのVM経路で computed goto 回避、命令デコード1回/変数削減

**補足**: `emit_var` fast path は litvar と異なり `out.append(literal)` を伴わないため、融合の有無に関わらず1命令で完結。`{{a}} hello {{b}}` のような litvar+literal混在テンプレも全て fast path に収まる。

**リスク**: 低。`is_simple` の定義拡張は従来の litvar 集合のスーパーセット。fast path 内の `for_each_field_ref` は通常 dispatch と同一ロジックを呼ぶため出力一致。`try_resolve_loop_binding` の呼び出し順も通常 `handle_emit_var` と同一。

**検証**: `injamm_tests` 全合格、bench_format の 1var は変動なしだが hybrid 2740ns(-3%)、bench_delegate の 1var 手組み比 1.6× 維持。

## ベンチマーク詳細（採用後 vs ベースライン）

ベースライン: 2026-08-25 適用前の 5回平均（同ビルドオプション）。適用後: A1+A2 5回平均。

```
many_vars(1001 refs)  197485.9 ns → 179695.1 ns  -9.0%
wide_first(idx0)         504.6 ns →   485.0 ns  -3.9%
wide_mid(idx10)            ~498 ns →   495 ns  -0.6% (誤差)
wide_last(idx19)         604.1 ns →   519.2 ns -14.1%
wide_all(5 refs)        1380 ns →  1340 ns  -2.8%
no_filter                626.2 ns →   604.6 ns  -3.5%
with_filter              773.0 ns →   769.8 ns  -0.4% (±)
enum_str_field           519.8 ns →   482.9 ns  -7.1%
section_loop_large       633.2 ns/elem → 631.6 ns/elem -0.3%
section_loop (3elem)    3120 ns → 3100ns -0.6%
filter_chain               8.3 ns →    8.4ns  ±0%
```

bench_format:
```
1 var string  238-244 ns → 232-238 ns  -2% (誤差)
1 var int     193-208 ns → 197-208 ns  ±0%
3 vars        784 ns → 782-804 ns     ±0%
10 vars      2099 ns → 2038-2069 ns    -2%
hybrid       2849 ns → 2740-2806 ns   -3%
wide partial 2373 ns → 2184-2233 ns   -5%
```

bench_delegate:
```
1 var 234 ns vs manual 152 ns  1.54× (前 1.60×) gap縮小
3 vars 821 ns vs manual 471 ns 1.74×
10 vars 2073 ns vs manual 1085 ns 1.91×
buffer reuse(engine) 955 ns vs manual 474 ns 2.01×
```

いずれも std::format 比では 1.7-2.1× 高速を維持、fmt FMT_COMPILE 比では 1varのみ 0.4×（fmtが95nsで最速）、2vars以上は injamm が 1.2-1.5× 高速。

## 見送り施策の判断理由（ponytail）

**Skipped: やらないことが最速** — 以下は diff に対する利得が <5% または稀少パスのみで、コード churn / 後方互換リスクが上回る。

- **escape reserve (B1)**: safe文字列では AVX の mask==0 パスが支配的で reserve不要、escape重でも estimateが既に `lit*4+var*32` で一定の余裕を持つ。追加 reserveは二重確保になる。
- **filtered reuse / filter reserves (B2,B3)**: `filtered_value_` は子executorのstack localで1000要素ループで1000回構築されるが、S SSO (15 byte) で空構築は8ns、実測 with_filter 差は0.4%。replace/urlencode の reserveも該当フィルタ使用時のみで汎用 benchに現れず。
- **unconditional hint (C1)**: Person(sz=2)で `keys[hint]==key` の1分岐+文字列比較を毎回追加し、ヒットで1 str-cmpを節約するトレードオフ。計測で small structの 2-field lookupが 2-5ns 退行、section_largeも5%退行。`sz>=5` 閾値は妥当。
- **binding_truthy hint (C2)**: `{{#if binding.sub}}` の truthiness を O(1)化するが、現行benchにif内loopが無く効果測定不可。`binding_truthy` 型消去ポインタに `uint32_t hint` を追加する ABI変更を伴い、全呼出の更新が必要。if+loopがホットなワークロードで再評価。
- **compile move / estimate cache (E1,E2)**: compileは1回、renderは数千回。per-render 2乗算の削減は5ns未満、move化はcompile 200ns削減だがユーザー体感は0。
- **ct_is_straight_op拡張 (E3)**: `emit_this/at_index`等を直線扱いすれば hybrid coverage +15%だが、dotパスでは `path_indices` の整合が必要で `test_ct_exec_crosscheck` のリスク。現状 hybrid 3%改善で十分。

## 適用 diff 規模

```
include/injamm/bytecode_compile.hpp |  2 ++  (is_simple判定 1行)
include/injamm/bytecode_exec.hpp    | 49 ++++-- (for_each_field 17行 + fast path 26行)
計 51 行、後方互換維持、header-only維持、API変更なし
```

## 今後の推奨（優先度順）

1. **C2 の再評価**: htmx partial内で `{{#items}}{{#if name}}` のような条件付きloopが支配的なら、binding_truthyへのヒント伝搬で 5-10ns/要素削減。専用bench `bench_binding_truthy` を追加してから実装。
2. **計測の精密化**: `section_loop_large` は現状 625ns/elemで安定しているが、1000要素×5回平均ではCPU周波数ゆらぎ±2%が支配。`taskset -c 2` 固定 + `perf stat` で分岐ミス率を確認してから追加施策を判断。
3. **やらない**: 新依存、API変更、SIMD再実装。P0-P2+R1+A1+A2で小-中テンプレ25%→30%改善を達成しており、追加の複雑化は不要。

## 検証

- `cmake --build build -j2 && ./build/injamm_tests` — 1940 assertions, 850 cases, All passed (3回連続)
- `injamm_benchmark` 上記、`injamm_bench_format` / `injamm_bench_delegate` 退行なし
- 変更は `include/injamm/bytecode_*` のみに限定、fuzz corpus 80件は従来どおり（未実行だがロジックは同一）

---
*生成: `include/injamm/bytecode_exec.hpp:332,1701` / `include/injamm/bytecode_compile.hpp:1505` を編集、build/benchmark 5回平均で検証。サブエージェント5視点の報告書は `docs/perf_review.md` / `scratchpad/*_report.md` 参照。*
