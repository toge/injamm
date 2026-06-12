# NTTP 版の CT Bytecode + VM Executor 統合 — Design Spec

## 概要

NTTP `render<fixed_string>` の描画ループが、VM (`engine<T>`) の computed goto dispatch に比べて
**20倍以上遅い**（~388ns vs ~20ns）問題を解決する。

`render<fixed_string>` のコンパイル時パース結果を**コンパイル時にバイトコード**
（`ct_bytecode<N>`）に変換し、実行時は VM executor (`bc_executor`) で描画する。
これにより:
- NTTP 版が VM 並の速度に（~20ns 台）
- 実行エンジンが `bc_executor` 1本に統一
- `ct_render.hpp`（~1000行）を削除

## 背景: なぜ NTTP は遅いのか

| 要因 | NTTP 版 | VM 版 |
|------|---------|-------|
| ディスパッチ | 関数呼び出し（chunk種別ごと） | computed goto（間接ジャンプ） |
| フュージョン | なし | `emit_litvar` で literal+var を1命令に |
| ストレージ | SoA (`std::array`) | 密な命令列 |

`render<fixed_string>` の render ループは `constexpr` で書かれているが、値は
ランタイムパラメータなので実際は通常の関数呼び出しになる。constexpr であることの
メリットはパースのゼロコストのみで、描画ループ自体は VM の computed goto に
敵わない。

## 設計

### Section 1: CT バイトコード形式

`bc_instruction` / `bc_opcode` / `string_filter_entry` / `int_filter_entry` /
`float_filter_entry` は constexpr friendly なのでそのまま再利用。

新規に追加する型:

```cpp
struct string_ref {
  const char* data = nullptr;
  std::size_t size = 0;
  constexpr string_ref() = default;
  constexpr string_ref(const char* d, std::size_t s) : data(d), size(s) {}
};

struct ct_var_ref {
  string_ref key;              // テンプレート文字列内のキーを指す
  std::uint32_t field_index = UINT32_MAX;
};

template <std::size_t N>
struct ct_bytecode {
  std::array<bc_instruction, N> instructions{};
  std::array<string_ref, N> literals{};     // テンプレート部分文字列を参照
  std::array<ct_var_ref, N> var_refs{};
  std::size_t instr_count{};
  std::size_t literal_count{};
  std::size_t var_ref_count{};
  error_ctx error{};
};
```

`string_ref` はテンプレート文字列の部分範囲を指す constexpr 安全なポインタ+長さ。
`fixed_string` NTTP の内部バッファは静的ストレージに配置されるため、実行時に
`string_ref` を deref しても安全。

### Section 2: コンパイル時パイプライン

既存の `ct_parse_into()` はそのまま使い、新たに **chunk → CT bytecode 変換**
を追加する:

```
テンプレート文字列 (NTTP fixed_string)
  │ consteval:
  │   ct_parse_into()              → ct_parsed_template<N>
  │   ct_chunks_to_bytecode<T>()   → ct_bytecode<N>
  │
  ├─ ct_chunks_to_bytecode() は chunk 配列を順次走査し、対応する
  │   バイトコード命令を生成する。bc_compiler::compile_body() の
  │   ロジックと等価だが、テキスト再スキャン不要。
  │
  ├─ fusion 最適化: 直近の命令が emit_literal の場合、emit_litvar / emit_litvar_raw
  │   に置き換える。
  │
  └─ セクション/if のネストは再帰的に処理（chunk 配列で flatten 済み）
```

#### ct_chunks_to_bytecode() の疑似コード

```
for each chunk in chunks:
  switch chunk.kind:
    literal:
      emit_literal(chunk.text_offset, chunk.text_length)
    placeholder:
      if last_instr is emit_literal:
        patch to emit_litvar/emit_litvar_raw
      else:
        emit_var/emit_var_raw
      if chunk has filters:
        emit_resolve_filtered → filter_*... → emit_filtered/emit_filtered_raw
    section:
      emit_section/emit_section_bool
      emit chunk_var_ref
      body_start = current_offset
      emit body recursively
      emit_end(backpatch body_start)
    inverted:
      emit_inverted
      emit chunk_var_ref
      body_start = current_offset
      emit body recursively
      emit_end(backpatch body_start)
    at_var:
      emit_at_index / emit_at_first / emit_at_last / emit_at_key / emit_at_root
    at_section:
      emit_at_section
      emit chunk_var_ref
      body_start = current_offset
      emit body recursively
      emit_end(backpatch body_start)
    if_else:
      emit_if
      emit chunk_var_ref
      body_start = current_offset
      emit body recursively
      emit_else(backpatch to end)
      emit body recursively
      emit_endif
    ct_break:
      emit_break
    ct_continue:
      emit_continue
```

### Section 3: 実行時変換

`ct_bytecode<N>` → `bytecode`（runtime 構造体）の変換:

```cpp
bytecode to_bytecode(const ct_bytecode<N>& ct) {
  bytecode bc;
  bc.instructions.assign(
    ct.instructions.begin(),
    ct.instructions.begin() + ct.instr_count
  );
  bc.literals.reserve(ct.literal_count);
  for (size_t i = 0; i < ct.literal_count; ++i) {
    bc.literals.emplace_back(ct.literals[i].data, ct.literals[i].size);
  }
  bc.var_refs.reserve(ct.var_ref_count);
  for (size_t i = 0; i < ct.var_ref_count; ++i) {
    bc_var_ref ref;
    ref.key.assign(ct.var_refs[i].key.data, ct.var_refs[i].key.size);
    ref.field_index = ct.var_refs[i].field_index;
    bc.var_refs.push_back(std::move(ref));
  }
  bc.error = ct.error;
  return bc;
}
```

この変換は `render<>()` の呼び出しごとに1回実行される。典型的なテンプレートでは
**数十〜数百回のムーブ/コピー** で完了し、コストは **〜50ns** 程度。

### Section 4: Fusion 最適化

`ct_chunks_to_bytecode()` 内で、現行の `bc_compiler::emit_var()` と同様の
fusion ロジックを実装する:

```
現在の chunk が placeholder で、かつ直近の命令が emit_literal の場合:
  - emit_literal を emit_litvar / emit_litvar_raw に書き換え
  - operand にリテラルインデックス、operand2 に変数参照インデックスを設定
```

これにより NTTP 版でも VM 版と同様の fusion 最適化が効く。

### Section 5: API 変更

`render<fixed_string>` の振る舞いは変わらない（シグネチャ互換、内部実装のみ変更）。

```cpp
template <injamm::fixed_string Tmpl, typename T>
auto render(T const& value) -> expected<std::string> {
  constexpr auto ct_bc = ct_chunks_to_bytecode<T>(ct_parse_into(Tmpl));
  if (ct_bc.error.ec != error_code::none)
    return std::unexpected(ct_bc.error);  // ここには来ない（consteval で弾く）
  auto bc = to_bytecode(ct_bc);
  return bc_execute(bc, value);
}
```

### Section 6: 削除するファイル

| ファイル | 行数 | 削除理由 |
|----------|------|----------|
| `include/injamm/ct_render.hpp` | ~980 | 全関数が不要に |

### Section 7: 新規作成するファイル

| ファイル | 推定行数 | 責務 |
|----------|----------|------|
| `include/injamm/bytecode_ct_compile.hpp` | ~250 | `ct_chunks_to_bytecode<T>()`, `to_bytecode()` |

### Section 8: 変更するファイル

| ファイル | 変更内容 |
|----------|----------|
| `include/injamm/escape_hatch.hpp` | `ct_render.hpp` → `bytecode_ct_compile.hpp` の include に差し替え。`render()` 内部で `bc_execute()` を呼ぶ。 |
| `include/injamm/bytecode_exec.hpp` | `bc_execute()` free function を公開。現在は `engine::render()` からのみ呼ばれている。 |

### Section 9: エラーハンドリング

`ct_chunks_to_bytecode()` は consteval で実行される。エラーはコンパイルエラー
として報告される（従来の `ct_parse_into()` と同じ挙動）。

`to_bytecode()` と `bc_execute()` は従来通りの `expected<std::string, error_ctx>`
を返す。

### Section 10: テスト戦略

既存の `test_injamm_ct.cpp`（1000+行、NTTP 用テスト）がすべてそのまま動作する
ことを確認する。加えて:

1. `ct_chunks_to_bytecode()` の出力と `bc_compiler::compile_body()` の出力が
   **意味的に等価** であることを検証する（命令列の比較、または同一入力に対する
   レンダリング結果の比較）。
2. ベンチマークで NTTP 版が `_bc` 相当の速度になったことを確認する。

## 非目標

- `engine<T>` の CT bytecode 対応（後日）
- `ct_parsed_template` の AoS 化（中間表現なので現状維持で十分）
- 命令数の最適化（fusion のみ）
