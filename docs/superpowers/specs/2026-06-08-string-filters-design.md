# injamm: String Filters (`|` Syntax) Design

## 概要

`injamm`にパイプ（`|`）記法による文字列フィルタ機能を追加する。

### サポートするフィルタ
- `to_upper`: ASCII小文字を大文字に変換
- `to_lower`: ASCII大文字を小文字に変換
- `trim`: 先頭末尾の空白（スペース・タブ）を除去
- `ltrim`: 先頭の空白（スペース・タブ）を除去
- `rtrim`: 末尾の空白（スペース・タブ）を除去

### 基本構文
```
{{var | to_upper}}
{{var | trim | to_upper}}   // チェーン可能
{{{var | to_lower}}}        // raw出力でもフィルタ適用
```

## 設計方針

既存の高速パスへの影響を最小限にするため、フィルタ付きプレースホルダーは既存の`emit_var`/`emit_var_raw`とは**完全に独立した新しいオペコード列**で処理する。

## セクション 1: フィルタの種別定義

`bytecode.hpp`にフィルタの種別を追加する。

```cpp
enum class string_filter : std::uint8_t {
  to_upper,  // ASCII大文字変換
  to_lower,  // ASCII小文字変換
  trim,      // 先頭末尾の空白除去
  ltrim,     // 先頭の空白除去
  rtrim      // 末尾の空白除去
};
```

`bc_var_ref`にフィルタ情報を追加する。

```cpp
struct bc_var_ref {
  std::string_view key;
  std::uint32_t field_index = UINT32_MAX;
  std::vector<string_filter> filters;  // フィルタチェーン
};
```

## セクション 2: パーサ変更

### BC版 (`parse.hpp`)

`chunk_placeholder`にフィルタリストを追加する。

```cpp
struct chunk_placeholder {
  std::string key;
  bool raw;
  std::vector<string_filter> filters;
};
```

プレースホルダーの解析時に`|`を検出してフィルタを分離する。

### NTTP版 (`ct_parse.hpp`)

`ct_parsed_template`にフィルタリスト配列を追加する。

```cpp
template <std::size_t MaxChunks>
struct ct_parsed_template {
  // 既存のメンバー...
  std::array<std::vector<string_filter>, MaxChunks> filters;  // 追加
};
```

プレースホルダーの解析時に`|`を検出してフィルタを分離する。

## セクション 3: コンパイラ変更

`bytecode_compile.hpp`で、フィルタ付きプレースホルダーを検出し、専用の命令列を生成する。

### フィルタなし（既存の高速パス）
```cpp
bc.add_instruction(bc_opcode::emit_var, 0, var_ref_idx);
```

### フィルタあり（新しいパス）
```cpp
// 1. resolve_filtered: 値を一時バッファに解決
bc.add_instruction(bc_opcode::resolve_filtered, 0, var_ref_idx);

// 2. フィルタ命令を順次発行
for (auto f : var_ref.filters) {
  switch (f) {
    case string_filter::to_upper: bc.add_instruction(bc_opcode::filter_upper); break;
    case string_filter::to_lower: bc.add_instruction(bc_opcode::filter_lower); break;
    case string_filter::trim:     bc.add_instruction(bc_opcode::filter_trim); break;
  }
}

// 3. emit_filtered: 一時バッファを out_ に追加
bc.add_instruction(raw ? bc_opcode::emit_filtered_raw : bc_opcode::emit_filtered);
```

## セクション 4: VMエグゼキュータ変更

`bytecode_exec.hpp`に一時バッファと新しいオペコードハンドラを追加する。

### 一時バッファの追加
```cpp
struct bc_executor {
  // 既存のメンバー...
  std::string filtered_value_;  // フィルタ処理用の一時バッファ
};
```

### 新しいハンドラ

```cpp
/** @brief フィルタ付き変数解決: 値を一時バッファに解決する */
L_resolve_filtered: {
  auto const& var_ref = var_refs[operand2];
  filtered_value_.clear();
  if (!resolve_value(filtered_value_, var_ref.key, value, loop)) {
    return std::unexpected(error_ctx{.ec = error_code::unknown_key});
  }
  ++pc;
  DISPATCH();
}

/** @brief ASCII大文字変換 */
L_filter_upper: {
  for (auto& c : filtered_value_) {
    if (c >= 'a' && c <= 'z') c -= 32;
  }
  ++pc;
  DISPATCH();
}

/** @brief ASCII小文字変換 */
L_filter_lower: {
  for (auto& c : filtered_value_) {
    if (c >= 'A' && c <= 'Z') c += 32;
  }
  ++pc;
  DISPATCH();
}

/** @brief 先頭末尾の空白除去 */
L_filter_trim: {
  auto start = filtered_value_.find_first_not_of(" \t");
  if (start == std::string::npos) {
    filtered_value_.clear();
  } else {
    auto end = filtered_value_.find_last_not_of(" \t");
    filtered_value_ = filtered_value_.substr(start, end - start + 1);
  }
  ++pc;
  DISPATCH();
}

/** @brief フィルタ後の文字列出力（エスケープあり） */
L_emit_filtered: {
  html_escape_into(out_, std::string_view{filtered_value_});
  ++pc;
  DISPATCH();
}

/** @brief フィルタ後の文字列出力（生出力） */
L_emit_filtered_raw: {
  out_.append(filtered_value_);
  ++pc;
  DISPATCH();
}
```

## セクション 5: NTTP版変更

### パーサ変更 (`ct_parse.hpp`)

プレースホルダーの解析時に`|`を検出してフィルタを分離する。

### レンダラ変更 (`ct_render.hpp`)

`ct_render_placeholder`内でフィルタを適用する。

```cpp
template <class Mode, class Buffer, class T, class RootT, std::size_t N>
constexpr auto ct_render_placeholder(Buffer& out, ct_parsed_template<N> const& chunks, std::size_t i,
                                      T const& value, RootT const& root_value, loop_state const* loop)
    -> std::expected<void, error_ctx> {
  auto const key = chunks.texts[i];
  bool raw = chunks.flags[i] != 0;
  auto const& filters = chunks.filters[i];

  // ... 既存の @root / @var 処理 ...

  // フィルタが存在する場合
  if (!filters.empty()) {
    std::string tmp;
    if (!resolve_value(tmp, key, value, loop)) {
      return std::unexpected(error_ctx{.ec = error_code::unknown_key});
    }
    // 型チェック: 文字列型でなければエラー
    // フィルタ適用
    for (auto f : filters) {
      switch (f) {
        case string_filter::to_upper:
          for (auto& c : tmp) if (c >= 'a' && c <= 'z') c -= 32;
          break;
        case string_filter::to_lower:
          for (auto& c : tmp) if (c >= 'A' && c <= 'Z') c += 32;
          break;
        case string_filter::trim:
          auto s = tmp.find_first_not_of(" \t");
          if (s == std::string::npos) tmp.clear();
          else { auto e = tmp.find_last_not_of(" \t"); tmp = tmp.substr(s, e - s + 1); }
          break;
      }
    }
    // 出力
    return {};
  }

  // フィルタなし: 既存の高速パス
  // ...
}
```

## セクション 6: エラー処理・テスト・エッジケース

### エラー処理

- 未知のフィルタ名: `error_code::unknown_filter`（新規追加）を返す
- 非文字列型へのフィルタ適用: `error_code::type_mismatch`を返す

### エッジケース

- `{{@index | to_upper}}`: `@index`は数値なのでエラー
- `{{@key | to_upper}}`: `@key`は文字列なので正常に動作
- `{{var | to_upper | to_lower}}`: フィルタチェーンが正しく適用される
- `{{{var | trim}}}`: raw出力でもフィルタが適用される

### テスト戦略

`tests/test_injamm.cpp`に以下を追加:

1. 基本的なフィルタ動作（`to_upper`、`to_lower`、`trim`）
2. フィルタチェーン（`trim | to_upper`）
3. raw出力とフィルタの組み合わせ
4. 非文字列型へのフィルタ適用でエラーが発生すること
5. 未知のフィルタ名でエラーが発生すること

## 影響範囲

### 変更ファイル
- `include/injamm/detail/bytecode.hpp`: `string_filter`列挙型、`bc_var_ref`にフィルタリスト、新しいオペコード追加
- `include/injamm/detail/bytecode_compile.hpp`: フィルタ付きプレースホルダーの検出と命令列生成
- `include/injamm/detail/bytecode_exec.hpp`: フィルタ専用ハンドラと一時バッファ追加
- `include/injamm/detail/parse.hpp`: `chunk_placeholder`にフィルタリスト追加、`|`の解析
- `include/injamm/detail/ct_parse.hpp`: `ct_parsed_template`にフィルタリスト追加、`|`の解析
- `include/injamm/detail/ct_render.hpp`: フィルタ適用ロジック追加
- `include/injamm/detail/types.hpp`: `error_code`に`unknown_filter`追加
- `tests/test_injamm.cpp`: フィルタ関連のテストケース追加

### 性能影響
- フィルタなしの既存テンプレート: 影響なし（既存の高速パスをそのまま使用）
- フィルタありのテンプレート: フィルタ適用のオーバーヘッドが発生するが、これは想定内の動作
