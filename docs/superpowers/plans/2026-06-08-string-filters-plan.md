# String Filters Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add pipe (`|`) syntax for string filters (`to_upper`, `to_lower`, `trim`, `ltrim`, `rtrim`) to both BC and NTTP versions of injamm.

**Architecture:** Filters are implemented as a completely separate bytecode path from existing placeholders. Non-filtered templates remain unchanged. Filtered templates go through: resolve → filter chain → emit.

**Tech Stack:** C++26, Glaze (reflection), Catch2 (testing)

---

## Task 1: Add string_filter enum and error code

**Files:**
- Modify: `include/injamm/detail/bytecode.hpp:15-39`
- Modify: `include/injamm/detail/types.hpp:13-21`
- Modify: `include/injamm/detail/parse.hpp:32-47`

- [ ] **Step 1: Add string_filter enum to bytecode.hpp**

Add after `bc_opcode` enum (before line 40):

```cpp
/**
 * @brief 文字列フィルタの種別
 * @details プレースホルダに適用する文字列変換の種類を定義する
 */
enum class string_filter : std::uint8_t {
  to_upper,  /**< ASCII小文字→大文字変換 */
  to_lower,  /**< ASCII大文字→小文字変換 */
  trim,      /**< 先頭末尾の空白除去 */
  ltrim,     /**< 先頭の空白除去 */
  rtrim      /**< 末尾の空白除去 */
};
```

- [ ] **Step 2: Add unknown_filter error code to types.hpp**

Modify `error_code` enum in types.hpp (line 13-21):

```cpp
enum class error_code : int {
  none = 0,
  no_read_input = 1,
  unexpected_end = 2,
  unknown_key = 3,
  syntax_error = 4,
  type_mismatch = 5,
  invalid_utf8 = 6,
  unknown_filter = 7,  // 追加
};
```

- [ ] **Step 3: Add parse_string_filter helper to parse.hpp**

Add after `parse_at_kind` function (after line 47):

```cpp
/**
 * @brief フィルタ名文字列を string_filter 列挙値に変換する
 * @param name フィルタ名（"to_upper" / "to_lower" / "trim" / "ltrim" / "rtrim"）
 * @return 対応する string_filter 列挙値、未知の場合は std::nullopt
 */
[[nodiscard]] constexpr std::optional<string_filter> parse_string_filter(std::string_view name) noexcept {
  if (name == "to_upper") return string_filter::to_upper;
  if (name == "to_lower") return string_filter::to_lower;
  if (name == "trim") return string_filter::trim;
  if (name == "ltrim") return string_filter::ltrim;
  if (name == "rtrim") return string_filter::rtrim;
  return std::nullopt;
}
```

Add `#include <optional>` at the top of parse.hpp.

- [ ] **Step 4: Run tests to verify no regressions**

Run: `cmake --build build && ctest --test-dir build -V`
Expected: All 31 test cases pass

---

## Task 2: Add filter opcodes and update bc_var_ref

**Files:**
- Modify: `include/injamm/detail/bytecode.hpp:15-113`

- [ ] **Step 1: Add filter opcodes to bc_opcode enum**

Add before `halt` in bc_opcode (line 37):

```cpp
  emit_at_key,        /**< @key 出力（ループ内の現在要素キー名 / インデックス文字列） */
  emit_this,          /**< 現在のコンテキスト自体のシリアライズ */
  resolve_filtered,   /**< フィルタ付き変数解決: 値を一時バッファに解決 */
  filter_upper,       /**< ASCII大文字変換 */
  filter_lower,       /**< ASCII小文字変換 */
  filter_trim,        /**< 先頭末尾の空白除去 */
  emit_filtered,      /**< フィルタ後の文字列出力（エスケープあり） */
  emit_filtered_raw,  /**< フィルタ後の文字列出力（生出力） */
  halt                /**< プログラム終了 */
```

- [ ] **Step 2: Add filters field to bc_var_ref**

Modify bc_var_ref struct (line 46-49):

```cpp
struct bc_var_ref {
  std::string_view key;                    /**< 変数名 */
  std::uint32_t field_index = UINT32_MAX;  /**< コンパイル時解決済みフィールドインデックス */
  std::vector<string_filter> filters;      /**< フィルタチェーン */
};
```

- [ ] **Step 3: Run tests to verify no regressions**

Run: `cmake --build build && ctest --test-dir build -V`
Expected: All 31 test cases pass

---

## Task 3: Update parser to handle pipe syntax

**Files:**
- Modify: `include/injamm/detail/parse.hpp:91-377`
- Modify: `include/injamm/detail/chunk.hpp` (add filters to chunk_placeholder)

- [ ] **Step 1: Add filters to chunk_placeholder in chunk.hpp**

Check chunk.hpp for chunk_placeholder definition. Add filters field:

```cpp
struct chunk_placeholder {
  std::string key;
  bool raw;
  std::vector<string_filter> filters;  // 追加
};
```

- [ ] **Step 2: Add split_by_pipe helper to parse.hpp**

Add after `find_toplevel_else` function:

```cpp
/**
 * @brief 文字列を '|' で分割し、各パートの前後空白を除去する
 * @param input 入力文字列
 * @return 分割された文字列のベクター
 */
[[nodiscard]] constexpr std::vector<std::string_view> split_by_pipe(std::string_view input) {
  std::vector<std::string_view> result;
  std::size_t pos = 0;
  while (pos < input.size()) {
    auto pipe = input.find('|', pos);
    if (pipe == std::string_view::npos) {
      result.push_back(trim_sv(input.substr(pos)));
      break;
    }
    result.push_back(trim_sv(input.substr(pos, pipe - pos)));
    pos = pipe + 1;
  }
  return result;
}
```

- [ ] **Step 3: Update placeholder parsing in parse_into**

Modify the placeholder parsing section (around line 374-376) to handle filters:

```cpp
/** 通常のプレースホルダー（フィルタ対応） */
auto parts = split_by_pipe(inner);
auto key = parts[0];
std::vector<string_filter> filters;
for (std::size_t fi = 1; fi < parts.size(); ++fi) {
  auto f = parse_string_filter(parts[fi]);
  if (!f) {
    // 未知のフィルタ: エラーチャンクを追加
    result.push_back(chunk_literal{std::string{"ERROR_UNKNOWN_FILTER"}});
    return;
  }
  filters.push_back(*f);
}
result.push_back(chunk_placeholder{std::string{key}, false, std::move(filters)});
```

- [ ] **Step 4: Update raw placeholder parsing for filter support**

Modify the raw placeholder section (around line 129-132) to handle filters:

```cpp
auto key = trim_sv(tmpl.substr(tag_start + 3, end - tag_start - 3));
auto parts = split_by_pipe(key);
auto actual_key = parts[0];
std::vector<string_filter> filters;
for (std::size_t fi = 1; fi < parts.size(); ++fi) {
  auto f = parse_string_filter(parts[fi]);
  if (!f) {
    result.push_back(chunk_literal{std::string{"ERROR_UNKNOWN_FILTER"}});
    pos = end + 3;
    break;
  }
  filters.push_back(*f);
}
result.push_back(chunk_placeholder{std::string{actual_key}, true, std::move(filters)});
```

- [ ] **Step 5: Run tests to verify no regressions**

Run: `cmake --build build && ctest --test-dir build -V`
Expected: All 31 test cases pass

---

## Task 4: Update compiler to generate filter opcodes

**Files:**
- Modify: `include/injamm/detail/bytecode_compile.hpp`

- [ ] **Step 1: Update compile_placeholder to handle filters**

Find the function that compiles placeholders. Add filter detection:

```cpp
// フィルタの有無で分岐
if (var_ref.filters.empty()) {
  // 既存の高速パス（変更なし）
  if (raw) {
    bc.add_instruction(bc_opcode::emit_var_raw, 0, var_ref_idx);
  } else {
    bc.add_instruction(bc_opcode::emit_var, 0, var_ref_idx);
  }
} else {
  // フィルタ専用パス
  bc.add_instruction(bc_opcode::resolve_filtered, 0, var_ref_idx);
  for (auto f : var_ref.filters) {
    switch (f) {
      case string_filter::to_upper: bc.add_instruction(bc_opcode::filter_upper); break;
      case string_filter::to_lower: bc.add_instruction(bc_opcode::filter_lower); break;
      case string_filter::trim:     bc.add_instruction(bc_opcode::filter_trim); break;
    }
  }
  bc.add_instruction(raw ? bc_opcode::emit_filtered_raw : bc_opcode::emit_filtered);
}
```

- [ ] **Step 2: Run tests to verify no regressions**

Run: `cmake --build build && ctest --test-dir build -V`
Expected: All 31 test cases pass

---

## Task 5: Update VM executor with filter handlers

**Files:**
- Modify: `include/injamm/detail/bytecode_exec.hpp`

- [ ] **Step 1: Add filtered_value_ buffer to bc_executor**

Add to bc_executor class:

```cpp
std::string filtered_value_;  // フィルタ処理用の一時バッファ
```

- [ ] **Step 2: Add dispatch table entries for new opcodes**

Update dispatch table to include new opcodes before halt.

- [ ] **Step 3: Implement L_resolve_filtered handler**

```cpp
L_resolve_filtered: {
  auto const& var_ref = var_refs[operand2];
  filtered_value_.clear();
  if (!resolve_value(filtered_value_, var_ref.key, value, loop)) {
    return std::unexpected(error_ctx{.ec = error_code::unknown_key});
  }
  ++pc;
  DISPATCH();
}
```

- [ ] **Step 4: Implement filter_upper handler**

```cpp
L_filter_upper: {
  for (auto& c : filtered_value_) {
    if (c >= 'a' && c <= 'z') c -= 32;
  }
  ++pc;
  DISPATCH();
}
```

- [ ] **Step 5: Implement filter_lower handler**

```cpp
L_filter_lower: {
  for (auto& c : filtered_value_) {
    if (c >= 'A' && c <= 'Z') c += 32;
  }
  ++pc;
  DISPATCH();
}
```

- [ ] **Step 6: Implement filter_trim handler**

```cpp
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
```

- [ ] **Step 6b: Implement filter_ltrim handler**

```cpp
L_filter_ltrim: {
  auto start = filtered_value_.find_first_not_of(" \t");
  if (start == std::string::npos) {
    filtered_value_.clear();
  } else {
    filtered_value_ = filtered_value_.substr(start);
  }
  ++pc;
  DISPATCH();
}
```

- [ ] **Step 6c: Implement filter_rtrim handler**

```cpp
L_filter_rtrim: {
  auto end = filtered_value_.find_last_not_of(" \t");
  if (end == std::string::npos) {
    filtered_value_.clear();
  } else {
    filtered_value_ = filtered_value_.substr(0, end + 1);
  }
  ++pc;
  DISPATCH();
}
```

- [ ] **Step 7: Implement emit_filtered handler**

```cpp
L_emit_filtered: {
  html_escape_into(out_, std::string_view{filtered_value_});
  ++pc;
  DISPATCH();
}
```

- [ ] **Step 8: Implement emit_filtered_raw handler**

```cpp
L_emit_filtered_raw: {
  out_.append(filtered_value_);
  ++pc;
  DISPATCH();
}
```

- [ ] **Step 9: Add fallback cases to switch statement**

Add cases for new opcodes in the fallback switch block.

- [ ] **Step 10: Run tests to verify no regressions**

Run: `cmake --build build && ctest --test-dir build -V`
Expected: All 31 test cases pass

---

## Task 6: Update NTTP parser to handle pipe syntax

**Files:**
- Modify: `include/injamm/detail/ct_parse.hpp`
- Modify: `include/injamm/detail/ct_chunk.hpp`

- [ ] **Step 1: Add filters to ct_parsed_template in ct_chunk.hpp**

Add filters array to the SoA structure:

```cpp
std::array<std::vector<string_filter>, MaxChunks> filters;
```

- [ ] **Step 2: Add push_placeholder with filters method**

Add to ct_parse_context:

```cpp
constexpr std::size_t push_placeholder(std::string_view key, bool raw, std::vector<string_filter> filter_list = {}) {
  auto idx = tmpl.size;
  tmpl.push_placeholder(key, raw);
  tmpl.filters[idx] = std::move(filter_list);
  return idx;
}
```

- [ ] **Step 3: Update placeholder parsing in ct_parse_into**

Modify the placeholder section to parse filters:

```cpp
// 通常のプレースホルダー（フィルタ対応）
auto parts = split_by_pipe(inner);
auto key = parts[0];
std::vector<string_filter> filter_list;
for (std::size_t fi = 1; fi < parts.size(); ++fi) {
  auto f = parse_string_filter(parts[fi]);
  if (!f) {
    return;  // 未知のフィルタ: スキップ
  }
  filter_list.push_back(*f);
}
ctx.push_placeholder(key, false, std::move(filter_list));
```

- [ ] **Step 4: Update raw placeholder parsing for filter support**

Modify the raw placeholder section to handle filters.

- [ ] **Step 5: Run tests to verify no regressions**

Run: `cmake --build build && ctest --test-dir build -V`
Expected: All 31 test cases pass

---

## Task 7: Update NTTP renderer to apply filters

**Files:**
- Modify: `include/injamm/detail/ct_render.hpp`

- [ ] **Step 1: Add filter application helper function**

Add helper function before ct_render_placeholder:

```cpp
template <class T>
constexpr void apply_string_filter(std::string& str, string_filter filter) {
  switch (filter) {
    case string_filter::to_upper:
      for (auto& c : str) {
        if (c >= 'a' && c <= 'z') c -= 32;
      }
      break;
    case string_filter::to_lower:
      for (auto& c : str) {
        if (c >= 'A' && c <= 'Z') c += 32;
      }
      break;
    case string_filter::trim: {
      auto start = str.find_first_not_of(" \t");
      if (start == std::string::npos) {
        str.clear();
      } else {
        auto end = str.find_last_not_of(" \t");
        str = str.substr(start, end - start + 1);
      }
      break;
    }
    case string_filter::ltrim: {
      auto start = str.find_first_not_of(" \t");
      if (start == std::string::npos) {
        str.clear();
      } else {
        str = str.substr(start);
      }
      break;
    }
    case string_filter::rtrim: {
      auto end = str.find_last_not_of(" \t");
      if (end == std::string::npos) {
        str.clear();
      } else {
        str = str.substr(0, end + 1);
      }
      break;
    }
  }
}
```

- [ ] **Step 2: Update ct_render_placeholder to handle filters**

Add filter handling before the standard placeholder logic:

```cpp
auto const& filters = chunks.filters[i];

// フィルタが存在する場合
if (!filters.empty()) {
  std::string tmp;
  if (!resolve_value(tmp, key, value, loop)) {
    return std::unexpected(error_ctx{.ec = error_code::unknown_key});
  }
  // フィルタ適用
  for (auto f : filters) {
    apply_string_filter(tmp, f);
  }
  // 出力
  if constexpr (std::is_same_v<Mode, mustache_tag>) {
    if (!raw) {
      html_escape_into(out, std::string_view{tmp});
    } else {
      out.append(tmp);
    }
  } else {
    out.append(tmp);
  }
  return {};
}
```

- [ ] **Step 3: Run tests to verify no regressions**

Run: `cmake --build build && ctest --test-dir build -V`
Expected: All 31 test cases pass

---

## Task 8: Add tests for string filters

**Files:**
- Modify: `tests/test_injamm.cpp`

- [ ] **Step 1: Add test for to_upper filter**

```cpp
TEST_CASE("filter: to_upper", "[filter]") {
  Context ctx;
  ctx.name = "hello world";
  auto result = injamm::render("{{name | to_upper}}", ctx);
  REQUIRE(result);
  REQUIRE(*result == "HELLO WORLD");
}
```

- [ ] **Step 2: Add test for to_lower filter**

```cpp
TEST_CASE("filter: to_lower", "[filter]") {
  Context ctx;
  ctx.name = "HELLO WORLD";
  auto result = injamm::render("{{name | to_lower}}", ctx);
  REQUIRE(result);
  REQUIRE(*result == "hello world");
}
```

- [ ] **Step 3: Add test for trim filter**

```cpp
TEST_CASE("filter: trim", "[filter]") {
  Context ctx;
  ctx.name = "  hello  ";
  auto result = injamm::render("{{name | trim}}", ctx);
  REQUIRE(result);
  REQUIRE(*result == "hello");
}
```

- [ ] **Step 3b: Add test for ltrim filter**

```cpp
TEST_CASE("filter: ltrim", "[filter]") {
  Context ctx;
  ctx.name = "  hello  ";
  auto result = injamm::render("{{name | ltrim}}", ctx);
  REQUIRE(result);
  REQUIRE(*result == "hello  ");
}
```

- [ ] **Step 3c: Add test for rtrim filter**

```cpp
TEST_CASE("filter: rtrim", "[filter]") {
  Context ctx;
  ctx.name = "  hello  ";
  auto result = injamm::render("{{name | rtrim}}", ctx);
  REQUIRE(result);
  REQUIRE(*result == "  hello");
}
```

- [ ] **Step 4: Add test for filter chaining**

```cpp
TEST_CASE("filter: chaining", "[filter]") {
  Context ctx;
  ctx.name = "  hello  ";
  auto result = injamm::render("{{name | trim | to_upper}}", ctx);
  REQUIRE(result);
  REQUIRE(*result == "HELLO");
}
```

- [ ] **Step 5: Add test for raw output with filter**

```cpp
TEST_CASE("filter: raw output", "[filter]") {
  Context ctx;
  ctx.name = "  <script>  ";
  auto result = injamm::render("{{{name | trim}}}", ctx);
  REQUIRE(result);
  REQUIRE(*result == "<script>");
}
```

- [ ] **Step 6: Add test for non-string type error**

```cpp
TEST_CASE("filter: non-string error", "[filter]") {
  Context ctx;
  ctx.count = 42;
  auto result = injamm::render("{{count | to_upper}}", ctx);
  REQUIRE_FALSE(result);
  REQUIRE(result.error().ec == injamm::error_code::type_mismatch);
}
```

- [ ] **Step 7: Add test for unknown filter error**

```cpp
TEST_CASE("filter: unknown filter error", "[filter]") {
  Context ctx;
  ctx.name = "hello";
  auto result = injamm::render("{{name | unknown_filter}}", ctx);
  REQUIRE_FALSE(result);
}
```

- [ ] **Step 8: Run all tests**

Run: `cmake --build build && ctest --test-dir build -V`
Expected: All tests pass including new filter tests

---

## Task 9: Final verification with benchmarks

**Files:**
- None (verification only)

- [ ] **Step 1: Copy headers to template-benchmark**

```bash
cp include/injamm/detail/bytecode.hpp \
   include/injamm/detail/bytecode_compile.hpp \
   include/injamm/detail/bytecode_exec.hpp \
   include/injamm/detail/parse.hpp \
   include/injamm/detail/ct_parse.hpp \
   include/injamm/detail/ct_render.hpp \
   include/injamm/detail/types.hpp \
   ~/src/template-benchmark/build/vcpkg_installed/x64-linux/include/injamm/detail/
```

- [ ] **Step 2: Rebuild benchmarks**

```bash
cmake --build ~/src/template-benchmark/build --parallel 4
```

- [ ] **Step 3: Run at_vars benchmark**

```bash
~/src/template-benchmark/build/bench_at_vars --benchmark_time_unit=ns --benchmark_repetitions=3
```

- [ ] **Step 4: Verify no performance regression**

Compare results with previous baseline. Non-filtered paths should show no change.

- [ ] **Step 5: Commit changes**

```bash
git add -A
git commit -m "feat(string-filters): add pipe syntax for to_upper, to_lower, trim filters"
```
