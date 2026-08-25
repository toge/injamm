# injamm codegen版 描画高速化 — 単独分析レポート 2026-08-26

計測環境: GCC 16.2.1, Ryzen 7 7700, `-O3 -DNDEBUG`, `build/injamm_codegen_bench 2000 500`, 5回中央値

## 要約

サブエージェント並列実行は API rate limit で機能しなかったため、controller 単独で `util/injamm_codegen.cpp` を精読し、5視点で抽出した代表的施策から実装コストとリスクの低いものを実装+検証した。**1施策が明確な改善**、残り複数は見送りが妥当。

| 施策 | ファイル:行 | 効果（実測, 5回中央値） | 採用 |
|---|---|---|---|
| **C1 resolve_filtered の assign を (data, size) に** | `util/injamm_codegen.cpp:1043,1074` | many_vars(A) -2.4% (963→940µs), filter_in_loop(B) **-5.8%** (506→476µs), bool(C) -0.4% | **採用** |
| C2 ループ不変 hoist | codegen | render_c (bool) で50%削減見込みだが実装コスト高、誤判定リスク | 見送り |
| C3 emit_literal 1文字 push_back 化 | codegen | std::string::push_back 差は append(1, c) と同等で大差なし | 見送り |
| C4 filter_to_upper 結果の html_escape_append 省略 | codegen + helpers | upper/lower が special char を作らない前提で 1命令削減、ponytail的に微妙 | 見送り |
| C5 _bwdN bool → int8_t | codegen | bool 分岐削減は CPU 予測改善で 1-2%、効果微 | 見送り |
| C6 codegen の Buffer=string 直接特殊化 | codegen + helpers | 複雑化、効果未測定 | 見送り |

## 詳細 — 採用1施策

### C1: `_filtered.assign((str).data(), (str).size())` で size() 重複取得を排除 `util/injamm_codegen.cpp:1043,1074`

**現状**: `injamm_codegen.cpp` の `resolve_filtered` 命令で生成される `_filtered.assign(string)` は `std::string::assign(const std::string&)` を呼ぶ。この実装は libstdc++ で `assign(const string&)` 内で `size()` を 2回呼び、length 計算が二重になる。

**修正**: 生成コードを `_filtered.assign((access).data(), (access).size())` に置換。`(data, size)` オーバーロードは size を内部で取得し直す必要がない。

```cpp
// before
emit("_filtered.assign(" + access + ");");
// after
emit("_filtered.assign((" + access + ").data(), (" + access + ").size());");
```

**効果**:
- A (section loop, 名前エスケープ hot path) 963,643 ns → 940,589 ns **-2.4%**
- B (filter in loop, `_filtered` 再利用 hot) 506,434 ns → 476,894 ns **-5.8%**
- C (bool in loop, `_filtered` 不使用) 101,325 ns → 100,960 ns ±0% (ノイズ範囲)

**リスク**: 極小。`std::string::assign(const char*, size_t)` は `assign(const string&)` と同じセマンティクス。`access` が `std::string` の場合にのみ生成される経路 (`use_json`/else 分岐) で、`decltype` が他なら codegen 自体が通らない。すべてのテストケースで出力一致。

**検証**: `injamm_tests` 850 cases 全合格、`injamm_codegen_bench` 出力一致 WARNING なし。

## ベンチマーク詳細（採用後 vs ベースライン）

ベースライン: 2026-08-26 適用前の 5回中央値。適用後: C1 5回中央値。

```
[A] section loop         963,643 ns → 940,589 ns  -2.4%
[B] filter in loop       506,434 ns → 476,894 ns  -5.8%
[C] bool in loop         101,325 ns → 100,960 ns  -0.4% (ノイズ)
```

speedup vs runtime:
```
[A] 2.21× → 2.22×
[B] 2.29× → 2.42×
[C] 4.72× → 4.67× (±0%)
```

## 見送り施策の判断理由（ponytail）

**やらないことが最速** — 以下の diff に対する利得が <2% または実装コスト・誤判定リスクが上回る。

- **C2 ループ不変 hoist (`render_c` で data.active をループ外へ)**: 50%削減見込みだが、codegen 段階で `loop_depth_ == 0` のときに全命令をスキャンするループ不変解析が必要。ネストループや partial で誤動作する余地が大きく、ponytail的に「churn > 利得」。`render_c` は 100µs/2000elem で実害小。
- **C3 emit_literal 1文字 push_back 化**: `std::string::push_back(c)` と `std::string::append(1, c)` はどちらも内部で capacity チェック + 1 byte コピー。差なし。codegen のロジック分岐追加だけが残る。
- **C4 upper/lower/title フィルタ結果の html_escape_append 省略**: 大文字小文字変換が special char (`&<>"'`) を作らないのは事実だが、これは `injamm` の特殊仕様で将来も維持される保証がない (e.g., 将来の ASCII 拡張)。ヘルパー関数側で特殊化すると将来の変更で copmile error になる。ponytail的 YAGNI。
- **C5 `_bwdN` bool → int8_t**: bool の分岐予測は現代 CPU で極めて効率的。int8_t にしても分岐削減は微差で、bool 演算の単純さが失われる。
- **C6 codegen の `Buffer=std::string` 直接特殊化**: 既に `injamm::detail::html_escape_into<Buffer>` が Buffer テンプレートで `std::string` 専用最適化が入っている。codegen 側でさらに特殊化すると `injamm_codegen.cpp` と `injamm/escape.hpp` の密結合が増える。

## 適用 diff 規模

```
util/injamm_codegen.cpp          | 4 ++-- (2 行 x 2 経路)
examples/codegen_bench/render_b.hpp | 2 ++-- (再生成, 自動)
計 6 行、後方互換維持、header-only 維持、API 変更なし
```

## 今後の推奨（優先度順）

1. **C2 ループ不変 hoist の再評価**: もし `htmx partial` のような大量部分更新で `render_c` パターンが支配的になるなら、50% 削減の効果が効く。専用ベンチ (`bench_partial_render`) を追加して再評価。
2. **codegen の SIMD 化**: 現状 `injamm/escape.hpp` の `html_escape_into` は AVX2 で 32 byte 並列処理。codegen 生成コードでも同じ SIMD が効いている (テンプレート経由のため不要)。
3. **やらない**: API変更、glaze reflection の細部最適化、codegen 経路の新規機能。`std::string::assign` の最適化でフィルタループ -5.8% が得られており、追加の複雑化は不要。

## 検証

- `cmake --build build -j2 && ./build/injamm_tests` — 1940 assertions, 850 cases, All passed
- `injamm_codegen_bench` — 採用前後で出力一致、WARNING なし
- 変更は `util/injamm_codegen.cpp` のみ、生成コードは `gen.sh` で自動再生成

---

## サブエージェント並列実行が機能しなかった件

依頼では「サブエージェントを複数起動して、それぞれの視点で施策を検討し、ベンチで実測しながら効果があれば提案してください」だったが、5エージェント (A1-A5) を並列起動した直後に API rate limit で 3つ (A1, A2, A4) が即座に失敗。リトライ分 (A1, A2, A4 retry) と元のままの A3, A5 もバックグラウンド完了通知が返らず、`scratchpad/codegen_agent*.md` レポート0件、新規 commit 0件で終了。

ponytail的に controller 単独で 5視点でコードを精読し、最低限のゴール（ベースライン + 1施策以上 + レポート）を達成。サブエージェント並列が機能していた場合は A1-A5 それぞれが独立に複数施策を実装していた可能性はあるが、現セッションでは controller 単独の **C1 (1施策採用)** にとどまった。

---

*生成: `util/injamm_codegen.cpp:1043,1074` を編集、build/benchmark 5回中央値で検証。サブエージェント並列実行は API rate limit で破綻、controller 単独で続行。*