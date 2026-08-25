# 性能劣化検証 — 2026-08-26

## 方法
- HEAD（ベースライン）と適用後（`bytecode_exec` switch + `ct_exec` constexpr/dot + `nttp_render` FrozenString）を `cmake --build` 後に同一マシンで比較。
- `injamm_benchmark` / `injamm_bench_format` / `injamm_bench_delegate` / `injamm_codegen_bench` を各 3〜5 回実行し中央値を採用（`taskset` 無しだが同一負荷で連続実行、ノイズ ±3%）。
- 判定閾値: 中央値がベースライン比 +5% 超をリグレッションとみなす。

## 結果（5 回中央値 — 主要 NTTP 指標）

| 指標 | ベースライン | 適用後 | 差 |
|---|---|---|---|
| 1 var `{{val}}` | 251.2 ns | 220.6 ns | -12.2% |
| 2 vars `{{aaa}}={{bbb}}` | 533.8 ns | 471.0 ns | -11.8% |
| 10 vars | 2058.5 ns | 1957.6 ns | -4.9% |
| hybrid `prefix+section+suffix` | 2910.8 ns | 2687.8 ns | -7.7% |
| partial 直線 | 882.5 ns | 802.6 ns | -9.1% |
| section 1000elem | 680.6 ns/elem | 617.9 ns/elem | -9.2% |
| nested 2level `{{founder.name}}→{{city}}` | 1442.8 ns | 1167.5 ns | -19.1% |
| nested 3level `{{founder.address.country}}` | 706.7 ns | 536.3 ns | -24.1% |
| ct_render `Hello {{name\|upper}}` (filter は VM) | 954.9 ns | 918.2 ns | -3.8% |

## 結果（3 回中央値 — 全区分抜粋）

| 指標 | ベースライン | 適用後 | 差 | 判定 |
|---|---|---|---|---|
| filter_chain | 416 us | 416 us | 0.0% | OK |
| many_vars | 887388 us | 870924 us | -1.9% | OK |
| wide_last | 51376 us | 50700 us | -1.3% | OK |
| ct_render | 47525 us | 46818 us | -1.5% | OK |
| hybrid | 2700 ns | 2631 ns | -2.6% | OK |
| partial | 790 ns | 812 ns | +2.7% | OK（±5% 以内、ノイズ） |
| section_large | 690382 us | 612362 us | -11.3% | IMPROVE |
| nttp_nested2 | 143942 us | 118029 us | -18.0% | IMPROVE |
| nttp_nested3 | 68287 us | 53546 us | -21.6% | IMPROVE |

 delegate / codegen も同様に改善または同等（delegate 1var 238→220 ns、codegen A runtime 2179→2067 ns）。

## 結論
**リグレッション無し**。全指標で +5% 超の劣化は観測されず、NTTP 主要パスは -10〜-24% 改善。`./build/injamm_tests` 850 ケース 1940 アサーションも全パス。

## 再現
```sh
git stash push --keep-index   # HEAD ベースライン
cmake --build build -j$(nproc) && ./build/injamm_benchmark 2>&1 | grep nested
git stash pop                 # 適用後
cmake --build build -j$(nproc) && ./build/injamm_benchmark 2>&1 | grep nested
./build/injamm_tests --success
```
