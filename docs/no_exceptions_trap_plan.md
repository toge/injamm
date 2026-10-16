# 例外 trap 解消の設計計画

## 背景

`-fno-exceptions` ビルド（WASI など）では、実行時に回復不能 OOM が trap に退化する。
現在は ho-t path を直接追記するゼロコスト設計を維持し、`bc_execute*` 境界で `out_of_memory` を返している。

本ドキュメントは、trap をより狭い範囲に confined する staged plan を定義する。
完全な expected 伝播は巨大な変更になるため、段階的に判断する。

## 現状の guard 配置

- `bc_execute`: coarse try/catch 1箇所
- `bc_execute_into`: coarse try/catch 1箇所
- `bc_execute_into_sink`: coarse try/catch 1箇所
- `types::message()`: OOM 時は空文字を返却
- `formatError()`: OOM 時は空文字を返却
- `disassemble()`: OOM 時は空文字を返却
- `serialize_value::vformat` 2サイトで OOM/trap 回避
- `bytecode_io` で `try_reserve` / `try_assign` を使用 (18 sites)

## 計画

### Phase 0: 現状維持（完了）

- docs に trap 劣化を明記
- `-fno-exceptions` テスト (test_noexpects) を維持

### Phase 1: trap の明示的局所化

- `injamm_trap()` の call sites を catalog
- *hot* `append` / `reserve` には trap 明示 annotation を増やさない
- `io` / `serialize` の preflight allocation を declarations only

### Phase 2: preferential fall-back (no panic)

- `try_reserve` を coarse boundary helper として全呼出に拡張
- no-exceptions path は `std::new_handler` or `max_size` check を利用

### Phase 3: selective expected propagation only in boundaries

- `message()` / `formatError()` を `expected<string, error_ctx>` に変更
- public compat wrappers で backwards compatibility のために proxy

### Phase 4: optional full propagation

- whole hot path を fallible 化 (remove all coarse guards)
- benchmark fallback と test re-validation

## 推定影響

- 通信 invoke: trap を eliminate するには 130+ append sites, 26 reserve sites, 3 boundary catches, 3 diag caches への fallible 化
- performance penalty: expected + 5-15%
- external integrators expect coarse behavior currently

## 決定ポイント

- keep trap  (最小変更)
- allow manual allocation check everywhere (大規模)
- hybrid approach: keep trap but only trap on deep path, boundary returns expected (現在仕様) ← 推奨

## 次ステップ

もし完全 elimination を proceed するときは:
1. hot path の append 全箇所を fallible helper へ移行（is_simple を含む）
2. `tests/test_noexpects.cpp` を fault injection で更新
3. docs を rewrite して trap 挙動を明記

このドキュメントは design plan であり、本体コードは変更していません。
