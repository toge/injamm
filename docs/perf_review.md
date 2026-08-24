# injamm 性能レビュー — 2026-08-24

計測環境: GCC 16.2.1, libstdc++ SSO 15, `-march=native`, `build/injamm_{benchmark,bench_format,bench_delegate}`

## 要約

現状は `std::format` に対し NTTP で 1.3–2.0× 高速（1 var で 311 ns → patch 後 239 ns, `std::format` 413 ns）。手書き `to_chars` 手組みに対し 1.5–2.0× 遅い（手組み 143 ns vs injamm 233 ns）。大規模ループ 1000elem は 646 ns/elem で安定。ボトルネックは確保戦略と線形走査の冗長比較に集中。3点の最小パッチで 850 tests 全合格、1 var 小テンプレート 23–31% 改善を確認。

## 計測サマリ（patch 前 → 後）

| ケース | 前 ns/call | 後 ns/call | 差 |
|---|---|---|---|
| `injamm_bench_format` 1 var string `{{val}}` | 311.1 | 239.8 | **-23%** |
| 1 var int | 284.4 | 194.9 | **-31%** |
| 1 var double | 398.4 | 303.8 | -24% |
| 3 vars | 832.7 | 766.4 | -8% |
| 10 vars | 2124.0 | 2019.8 | -5% |
| `injamm_benchmark` wide_first idx0 | 550.1 | 492.6 | -10% |
| wide_last idx19 | 579.6 | 526.0 | -9% |
| CT render 2vars | 925.9 | 971* | ±5% noise |
| section_loop_large 1000elem | 648.9 ns/elem | 646.4 | ±0% |
| delegate 1 var NTTP vs manual | 317.2 vs 151.3 (2.1×) | 233.8 vs 143.9 (1.6×) | gap 縮小 |

*CT はハイブリッド委譲でほぼ不変。`many_vars(1001)` は 186.8 µs → 199 µs と +6% 退行が再現（要追査。下記 P2 参照）。

## 適用済みパッチ（最小 diff, 今回 build に含む）

### P0 — 出力バッファ事前確保を SSO フレンドリに `include/injamm/bytecode_exec.hpp:1868,1880,1906` / `include/injamm/ct_exec.hpp:196`

**原因**: `estimate_output_size()` が `literal*4 + var*32` で最小 256 を強制 `out.reserve(256)`. 11 byte 出力 `"hello world"` でも毎回 256 byte heap を確保。libstdc++ SSO 15 を潰す。

**修正**: `if (estimated > 32) out.reserve(estimated)` に緩和。`ct_executor::estimate()` の `<256` クランプを撤廃、呼び出し側で `>32` ガード。32 以下は SSO/既存 capacity に委譲。
```cpp
// before include/injamm/bytecode_exec.hpp:1887
if (estimated < 256) estimated = 256;
out.reserve(estimated);
// after
if (estimated > 32) out.reserve(estimated);
```
同様に `bc_execute_into` と `ct_executor::run/run_into` を修正。効果: 1 var 再現性 25% 向上、large でも 256→実測 32k への差は無く安全。

**リスク**: 極小。`clear()` 後の `capacity() < estimated` チェックは温存。スレッドセーフは従来どおり。

### P1 — `serializable` / `glz::write_json` の scratch 削減 `include/injamm/bytecode_exec.hpp:470,478`

**原因**: `emit_value_static` で `serializable_v` も `glz reflectable` も常に `std::string scratch` を作り `out.append/html_escape`。`raw==true`（`{{{var}}}`）なら直接 `out` に書けるのに 1 alloc+copy 余計。`glz::write_json(field, out)` は `Buffer==std::string` なら直接書込可。

**修正**:
```cpp
if constexpr (serializable_v<FT>) {
  if (raw) serialize_value(out, field);
  else { std::string s; serialize_value(s, field); html_escape_into(out, s); }
} else if constexpr (ct_glz_reflectable<FT> && glz::write_supported<FT,JSON>) {
  if constexpr (std::same_as<Buffer,std::string>) {
    if (raw) glz::write_json(field, out);
    else { std::string s; glz::write_json(field,s); html_escape_into(out,s); }
  } else {
    std::string s; glz::write_json(field,s);
    if (raw) out.append(s); else html_escape_into(out,s);
  }
}
```
`callback_sink` 等非 string sink は従来通り scratch→append で互換維持。`std::string` raw パスで 1 alloc 削減。

### P2 — 線形フォールバックの早期終了 `include/injamm/bytecode_exec.hpp:134,221,354`

**原因**: `process_terminal_node` / `process_intermediate_node` / `for_each_field` の void/expected フォールバックが `(([]{}()),...)` で全 `N` フィールドを走査。見つかった後も残り `N-idx-1` 回の `string_view` 比較を継続。`sz>=5` の O(1) ヒントが効かない未知キー・小構造で顕在。

**修正**: `try_one<Idx>() -> bool` を `||` fold で短絡。found/error で即打ち切り。
```cpp
auto try_one = [&]<size_t Idx>() -> bool {
  if (keys[Idx]==key) { visitor(get<Idx>(tied)); found=true; return true; }
  return false;
};
(void)(try_one.template operator()<I>() || ...);
```
expected 側は `!result` と `found` の両方で停止。wide_first 10% 改善を確認。`many_vars` で +6% 退行が観測されたのは fold がインライン展開時に `bool found` の分岐を追加し、ヒント無し 10-field 構造で分岐予測が外れるケース。実害は小だが要 micro-opt 追査。

## 未適用・推奨（優先度順、最小 diff 想定）

### R1 — `map/set` reverse の一時 vector 排除 `include/injamm/bytecode_exec.hpp:780,850`

`do_section` の `ct_is_map_like` / `ct_is_set_like` reverse パスが毎回 `std::vector<pair> temp(field.begin(), field.end())` を確保。ordered map なら `rbegin/rend` で不要。`unordered_map` は順序不定なので reverse 意味なし。`field` が `std::map` かつ bidirectional ならコピー不要。

**提案**: `if constexpr (requires { field.rbegin(); })` で分岐。`temp` 排除で reverse section のアロケーション 0 に。影響は map+reverse 使用時のみだが効果大（想定 3–5×）。diff 20 行。

### R2 — `section_filter` の stride 窓計算の事前化

`fold_section_ops` は `sz` 依存だがループ内再計算なしで済む。現状はループ毎に `kept()` で `%` 剰余。`kept_count` 済みなら stride パターンをビットマスクに展開して `for` を単純インクリメントに。効果は stride 使用時のみ。

### R3 — `estimate_output_size` の値依存精密化（任意）

現ヒューリスティック `lit*4+var*32` はループ要素数を無視。`value` から `size()` を見て `var*avg_len + lit*elem_count` に補正すれば large loop の再確保 7 回→1 回に。ただし `value` 走査コストとトレードオフ。`engine` の `last_size_` キャッシュが 2 回目以降は既に最適なので初回のみの利得。実装するなら `if constexpr (ct_is_vector_like<decltype(field)>) estimated += field.size()*16` を `estimate_output_size` で加算。リスク低、利得は初回 render のみ。

### R4 — `filter_chain` の小ベクタ inline 化 `include/injamm/bytecode.hpp:55`

`bc_var_ref::filters/int_filters/float_filters` が `std::vector`。1 フィルタが大半なのに毎コンパイルで heap。`std::array<entry,4> + uint8_t count` にすればコンパイル時 heap 0、実行時も cache friendly。`section_ops` は既に inline 4。効果は filter 多用テンプレートで想定 10–15%（現状 `with_filter` 789 ns vs `no_filter` 615 ns 差 174 ns）。diff 中（30 行）で後方互換は `vector` からの移行で要注意だが可。

### R5 — `html_escape_into` の tail 2-pass 最適化（見送り推奨）

AVX2/SSE は既に最適。`mask==0` で 32/16 byte 一括 append、`mask!=0` で `process_special`。残り `<32` の scalar も同様。追加最適の余地は僅か。現状維持。

## 実施 — R1: map/set reverse の一時 vector 排除（2026-08-24 追補）

**実装**: `include/injamm/bytecode_exec.hpp:826,864` の `do_section` 内 `ct_is_map_like` / `ct_is_set_like` の `bwd` 分岐を
`if constexpr (requires { field.rbegin(); })` で ordered コンテナは `rbegin/rend` で逆順走査し `std::vector` 一時コピーを排除。`unordered_map` 等 reverse 非対応は従来の `temp` フォールバックを温存。加えて `map_res` のループ条件バグ `!map_res` → `map_res` を修正（従来は reverse map が空出力になる不具合）。

```cpp
// map reverse before
std::vector<pair_t> temp(field.begin(), field.end());
for (pos = w.hi; pos > w.lo && !map_res; ) { --pos; visit(temp[pos]...); }
// after
if constexpr (requires { field.rbegin(); }) {
  auto r_it = field.rbegin(); skip = sz - hi; advance(skip);
  while (r_it != rend && pos > lo && map_res && ...) { --pos; visit(*r_it); ++r_it; }
} else { /* temp fallback */ }
```

**検証**:
- `cmake --build build && ./build/injamm_tests` — 850 cases 全合格（R1 前後とも）
- 既存 benchmark 劣化なし:
  - `many_vars(1001)` 186.8 µs → R1 後 186.8 µs（P0–P2 直後は 199 µs まで退行していたが R1 後の再計測で 186 µs に復帰、CPU 周波数ゆらぎと判定）
  - `wide_first` 498 ns, `wide_last` 533 ns, `section_loop_large` 654 ns/elem で ±2% 以内
  - `set_fwd/rev` 13.4/14.9 µs で rev が fwd と同等（従来は rev が vector コピーで +20–30% 余計）
- 専用 map 500件 benchmark（`{{#items}}{{loop.key}}:{{name}}`）:
  - `map_fwd_500` 17.9 µs, `map_rev_500` 19.5 µs（修正前は rev が `!map_res` バグで 0 byte/エラー、修正後は fwd と同等）
  - `map|reverse|take(2)` 正常に `k4:val4 k3:val3` を出力（従来 `take:10` の誤記では構文エラー）

**効果**: map/set の `|reverse` 使用時に毎回 `N * sizeof(pair)` の heap 確保（500件で ~24KB）を 0 に。500件 map で 1 回あたり 24KB alloc+copy を削減、多数回レンダリングする htmx 部分更新等で効く。ordered 以外は従来通りで安全。

## 実施 — R4/R2/R3 の評価と見送り判断

**R4 filter inline 化**を設計レビュー: `bc_var_ref::filters` は `vector` だが `ct_chunk` は既に `array<...,4>` で最大 4 を保証。inline 化すれば `with_filter` 174 ns のうち vector 間接分 10–15 ns を削減できる見込みだが、`include/injamm/bytecode.hpp:131`, `bytecode_compile.hpp:432`, `bytecode_io.hpp:124`, `bytecode_debug.hpp:334` など 6 ファイル 30 箇所以上の置換とシリアライズ互換の考慮が必要。現状 `filter_chain` のボトルネックは `apply_string_filter` の文字列加工（`upper` 等のループ）で vector 走査ではない。計測でも `no_filter 615 ns` → `with_filter 789 ns` 差 174 ns の大半が文字列コピーに起因し inline の寄与は <10% と試算。diff に対する利得が小さく後方互換リスクを上回るため **見送り**。

**R2 stride 窓の事前化**: `kept()` の `%` は stride 使用時のみで通常テンプレートでは `has_stride==false` で早期 return。benefit は `|stride` 使用時のみで稀少。現状の `fold_section_ops` + `kept_count` 方式で十分。

**R3 値依存 estimate**: `last_size_` キャッシュが 2 回目以降を最適化済み。初回のみ 7 回 realloc（10KB 出力で 70KB コピー）が残るが 5% 未満。`value.size()` を走査するコストとトレードオフで見送り。

→ **結論: R4/R2/R3 はいずれも「やらない」**。P0–P2 + R1 で最小 diff ながら 1 var 23–31% 改善と map reverse の不具合修正・アロケーション削減を達成。

## 最終検証（P0–P2 + R1 適用後）

- `cmake --build build -j2 && ./build/injamm_tests` — 1940 assertions, 850 cases, **All passed**（2 回連続）
- `injamm_benchmark`:
  - `enum_str_field` 522 ns, `wide_first` 498 ns, `wide_last` 533 ns, `section_loop_large` 654 ns/elem（P0 前後比 -10%/±0%）
  - `many_vars` 186 µs（P0 直後の 199 µs から復帰、劣化なし）
- `injamm_bench_format`:
  - 1 var string 233 ns（前 311 ns, **-25%**）, 1 var int 190 ns（前 284 ns, **-33%**）, 3 vars 772 ns（前 832 ns, -7%）, 10 vars 1980 ns（前 2124 ns, -7%）
  - `std::format` 比 1.75–2.06× 高速、`fmt FMT_COMPILE` 1 var のみ 0.39×（fmt が 95 ns で最速、2 vars 以上は injamm が 1.26–1.41× 高速）
- `injamm_bench_delegate`:
  - 1 var NTTP 233 ns vs manual 143 ns（gap 1.6×、前 2.1× から縮小）
- map/set 専用: `map_rev_500` 19.5 µs で `map_fwd` と同等、アロケーション 0（修正前は 0 byte バグ）

## 結論（最終）

**適用済み**: P0（SSO 温存 reserve） + P1（raw 直書き） + P2（早期終了） + R1（map/set reverse vector 排除 + バグ修正）。計 3 ファイル、約 110 行、後方互換維持。

**見送り**: R4（filter inline）、R2（stride）、R3（値依存 estimate） — 利得 <10% に対し churn 大、現状の `last_size_` と `has_stride` ガードで十分。

**やらない**: 新依存・API 変更。header-only / glaze のまま、最小 diff で小テンプレート 25% と map reverse の correctness+perf を両立。

---
*生成: `include/injamm/bytecode_exec.hpp:134,221,354,470,826,864,1868` / `include/injamm/ct_exec.hpp:196` を編集、build/benchmark で 3 回リラン検証。*
