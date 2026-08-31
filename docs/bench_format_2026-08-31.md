> **Note (2026-08-31): 本ベンチマークは `~/src/template-benchmark/bench/bench_format.cpp` に移設されました（`std::format` / `fmt::format(FMT_COMPILE)` との比較はテンプレートエンジン横断ベンチの範疇）。今後の再計測・更新は `template-benchmark` 側で行います。本ファイルはアーカイブとして残置。**

# bench_format 再計測 — 2026-08-31（runtime 追加・NTTP専用アンロール明示）

計測: `examples/bench_format.cpp` の injamm **NTTP (ct unroll)** vs **engine (VM)** vs `std::format` vs `fmt::format(FMT_COMPILE)` を Release で実行。NTTP は `include/injamm/ct_exec.hpp` の専用アンロール（`ct_executor` / `ct_hybrid_executor`）、engine は `bytecode_exec` VM。同一テンプレート・同一データで NTTP と engine を並置し、アンロールの有無の差を定量化する。

## 環境

- CPU: AMD Ryzen 7 1700 Eight-Core Processor (16 logical, 3.75 GHz max)
- Compiler: g++ (GCC) 16.2.1 20260819 (Red Hat 16.2.1-2)
- Build: `cmake -B build-bench-format -S . -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=~/vm/vcpkg/scripts/buildsystems/vcpkg.cmake -DBENCH_FMT=ON -DENABLE_THREADED_DISPATCH=ON`, `-O3 -DNDEBUG -march=native -std=gnu++23`
- vcpkg: glaze 7.8.3, fmt 12.2.0, enchantum 0.4.0#1
- 実行: `./build-bench-format/injamm_bench_format` 単回（詳細は `/tmp/bench-2026-08-31.log`）。`2026-08-30` は 7 回中央値、今回は runtime 追加後の単回値を併記。値は変動するが比率は安定。

## 変更点（2026-08-31）

- `examples/bench_format.cpp` に **全ケース（1 var string/int/double, 2 vars ×3, 10 vars）で `engine<T>` 計測を追加**。従来は 3 vars のみだった。
- サマリに 3 段出力: `NTTP vs std::format` / `engine vs std::format` / `NTTP vs engine`（アンロール効果）。
- `USE_CASES.md §6` の表も NTTP / engine / NTTP vs engine の 3 本に再構成。

## 単回サマリ（2026-08-31 Release 単回、ratio = engine or format / NTTP）

### NTTP (ct unroll) vs std::format

| ケース | NTTP ns | std::format ns | ratio (format/NTTP) | 判定 |
|---|---|---:|---:|---|---|
| 1 var (string) `{{val}}` | 60.6 | 37.2 | 0.61x | format faster（変動域、中央値では NTTP 0.71x）|
| 1 var (int) | 25.4 | 39.7 | 1.56x | NTTP faster |
| 1 var (double) | 33.4 | 139.7 | 4.18x | NTTP faster |
| 2 vars (str/int) | 47.3 | 136.6 | 2.89x | NTTP faster |
| 2 vars (str/dbl) | 57.0 | 205.3 | 3.60x | NTTP faster |
| 2 vars (int/dbl) | 60.4 | 238.6 | 3.95x | NTTP faster |
| 3 vars (str/int/dbl) | 71.8 | 266.0 | 3.71x | NTTP faster |
| 10 vars (5str/3int/2dbl) | 176.0 | 853.5 | 4.85x | NTTP faster |
| 3 vars escaped `{{aaa}}` | 168.3 | 264.5 | 1.57x | NTTP faster（escape コスト含む）|
| 3 vars raw `{{{aaa}}}` | 74.1 | 264.5 | 3.57x | NTTP faster |

### runtime engine (VM) vs std::format

| ケース | engine ns | std::format ns | ratio (format/engine) | 判定 |
|---|---|---:|---:|---|---|
| 1 var (string) | 62.2 | 37.2 | 0.60x | format faster |
| 1 var (int) | 50.5 | 39.7 | 0.79x | format faster |
| 1 var (double) | 80.4 | 139.7 | 1.74x | engine faster |
| 2 vars (str/int) | 110.7 | 136.6 | 1.23x | engine faster |
| 2 vars (str/dbl) | 119.4 | 205.3 | 1.72x | engine faster |
| 2 vars (int/dbl) | 134.9 | 238.6 | 1.77x | engine faster |
| 3 vars | 150.2 | 263.6 | 1.75x | engine faster |
| 10 vars | 554.8 | 853.5 | 1.54x | engine faster |
| 3 vars reuse buf vs format_to | 128.0 | 288.3 | 2.25x | engine faster |

### NTTP (ct unroll) vs engine (VM) — NTTP専用アンロール効果

| ケース | engine ns | NTTP ns | ratio (engine/NTTP) | 判定 |
|---|---|---:|---:|---|---|
| 1 var (string) | 62.2 | 60.6 | **1.03x** | ほぼ同等（単回ノイズ、他回では 1.9x）|
| 1 var (int) | 50.5 | 25.4 | **1.99x** | NTTP faster |
| 1 var (double) | 80.4 | 33.4 | **2.41x** | NTTP faster |
| 2 vars (str/int) | 110.7 | 47.3 | **2.34x** | NTTP faster |
| 2 vars (str/dbl) | 119.4 | 57.0 | **2.10x** | NTTP faster |
| 2 vars (int/dbl) | 134.9 | 60.4 | **2.23x** | NTTP faster |
| 3 vars | 150.2 | 71.8 | **2.09x** | NTTP faster |
| 10 vars | 554.8 | 176.0 | **3.15x** | NTTP faster |
| 3 vars reuse buf | 128.0 | 69.8 | **1.83x** | NTTP faster |

> 別回（直前 run）では 1 var string 1.92x / 3 vars 1.91x / 10 vars 3.19x。直線テンプレートでは **1.7–3.2x** が安定。10 vars で差が最大（変数展開の dispatch 回数が多いため）。

### fmt::format (FMT_COMPILE) vs injamm

| ケース | fmt ns | NTTP ns | ratio (fmt/NTTP) | fmt vs engine ns | ratio (fmt/engine) |
|---|---|---:|---:|---|---:|
| 1 var (string) | 13.5 | 60.6 | 0.22x fmt faster | 13.5 vs 62.2 | 0.22x |
| 2 vars (str/int) | 57.3 | 47.3 | **1.21x** NTTP faster | 57.3 vs 110.7 | 0.52x |
| 3 vars | 92.5 | 71.8 | **1.29x** | 92.5 vs 150.2 | 0.62x |
| 10 vars | 191.2 | 176.0 | **1.09x** | 191.2 vs 554.8 | 0.34x |

## 7回中央値との対照（2026-08-30）

| ケース | 08-30 中央値 NTTP | 08-30 中央値 format | 08-30 ratio | 08-31 単回 NTTP | 08-31 単回 format | 08-31 ratio |
|---|---:|---:|---|---:|---:|---|
| 1 var string | 59.3 | 42.7 | 0.71x | 60.6 | 37.2 | 0.61x |
| 1 var int | 25.2 | 40.5 | 1.60x | 25.4 | 39.7 | 1.56x |
| 3 vars | 73.0 | 277.8 | 3.78x | 71.8 | 266.0 | 3.71x |
| 10 vars | 194.2 | 873.6 | 4.48x | 176.0 | 853.5 | 4.85x |
| fmt 1 var | 13.4 vs 59.3 (0.23x) | | | 13.5 vs 60.6 (0.22x) | | |

中央値と単回の傾向は一致。`engine` は 08-30 では 3 vars のみ（157.9 ns vs 276.8 ns, 1.73x / reuse 136.9 vs 293.1 2.15x）、08-31 で全ケースに拡張し、上記「engine vs format」「NTTP vs engine」が新規。

## 詳細ログ（単回、ns/call）

```
1 var string NTTP 60.6 / format 37.2 / engine 62.2
1 var int NTTP 25.4 / format 39.7 / engine 50.5
1 var double NTTP 33.4 / format 139.7 / engine 80.4
2 vars str/int NTTP 47.3 / format 136.6 / engine 110.7
2 vars str/dbl NTTP 57.0 / format 205.3 / engine 119.4
2 vars int/dbl NTTP 60.4 / format 238.6 / engine 134.9
3 vars NTTP 71.8 / format 266.0 / engine 150.2
10 vars NTTP 176.0 / format 853.5 / engine 554.8
fmt FMT_COMPILE 1 var 13.5 / 2 vars 57.3 / 3 vars 92.5 / 10 vars 191.2
escaped 168.3 / raw 70.3 / raw 3 vars 74.1 / format 264.5
reuse: NTTP 69.8 / engine 128.0 / format_to 288.3
hybrid: NTTP 459.5 / engine 464.2 / NTTP raw 414.2
partial unroll: NTTP 123.0 / engine 127.9
wide partial 8col: NTTP 402.4 / engine 401.2
```

hybrid / partial は NTTP でも VM フォールバックまたは部分的委譲のため、NTTP と engine の差が小さい（直線 only でのみアンロールが効く）。

## 注意

- `{{var}}` は HTML エスケープあり、`std::format` はなし。公平比較は `{{{var}}}` raw。
- NTTP アンロールは単純テンプレート（変数置換のみ）に限定。セクション/if を含むと VM フォールバックで engine と同等。
- 再計測は `cmake -B build-bench-format -S . -DCMAKE_BUILD_TYPE=Release -DBENCH_FMT=ON -DENABLE_THREADED_DISPATCH=ON && cmake --build build-bench-format -t injamm_bench_format && ./build-bench-format/injamm_bench_format` で再現。7 回中央値を取る場合は `for i in 1..7; do ./build-bench-format/injamm_bench_format | tee bench-run-$i.log; sleep 1; done`。
