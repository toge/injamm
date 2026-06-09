# If Filter Chain Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add filter chain support to `{{#if expr}}` conditions, enabling `{{#if age | is_neg}}` and `{{#if age | mod(4) | eq(2)}}`.

**Architecture:** Reuse the existing filter infrastructure (string_filter, int_filter, float_filter enums and their apply functions) by extending `chunk_if` and `ct_parsed_template` to store filter chains, then applying filters before truthiness evaluation in both the bytecode VM and compile-time renderer.

**Tech Stack:** C++26, Glaze, Catch2, vcpkg

---

## File Map

| File | Responsibility | Changes |
|------|---------------|---------|
| `include/injamm/bytecode.hpp` | Filter enums, opcodes, var_ref | Add `is_neg`/`eq` to `int_filter`, add opcodes |
| `include/injamm/parse.hpp` | Runtime parser | Parse filter chains in `{{#if expr}}` |
| `include/injamm/chunk.hpp` | Runtime AST | Add filter vectors to `chunk_if` |
| `include/injamm/ct_chunk.hpp` | Compile-time AST (SoA) | Extend `push_if()` to accept filters |
| `include/injamm/ct_parse.hpp` | Compile-time parser | Pass filters through to `push_if()` |
| `include/injamm/bytecode_compile.hpp` | Bytecode compiler | Emit filter instructions in `compile_if()` |
| `include/injamm/bytecode_exec.hpp` | Bytecode VM | Apply filters in `L_emit_if`, add new opcodes |
| `include/injamm/ct_render.hpp` | Compile-time renderer | Apply filters in `ct_render_if()` |
| `tests/test_injamm.cpp` | Tests | Add filter-in-if test cases |

---

## Task 1: Add `is_neg` and `eq` to filter enums and opcodes

**Files:**
- Modify: `include/injamm/bytecode.hpp:97-105` (int_filter enum)
- Modify: `include/injamm/bytecode.hpp:15-62` (bc_opcode enum)

- [ ] **Step 1: Add new int_filter enum values**

In `include/injamm/bytecode.hpp`, add `is_neg` and `eq` to the `int_filter` enum after `numify`:

```cpp
enum class int_filter : std::uint8_t {
  abs,     /**< 絶対値 */
  hex,     /**< 16進数表記 */
  oct,     /**< 8進数表記 */
  bin,     /**< 2進数表記 */
  neg,     /**< 符号の逆転 */
  mod,     /**< 余り（引数: 除数） */
  numify,  /**< 3桁ごとにカンマ区切り */
  is_neg,  /**< 負数判定（真偽値出力: "true"/"false"） */
  eq       /**< 等価判定（引数: 比較値、真偽値出力: "true"/"false"） */
};
```

- [ ] **Step 2: Add new bc_opcode values**

In the `bc_opcode` enum, add new opcodes after `filter_int_numify`:

```cpp
filter_int_is_neg,  /**< 負数判定: "true"/"false" を出力 */
filter_int_eq,      /**< 等価判定: 引数と比較し "true"/"false" を出力 */
```

- [ ] **Step 3: Add computed goto labels and dispatch table entries**

In `bytecode_exec.hpp`, add dispatch table entries for the new opcodes (after `&&L_filter_int_numify`):

```cpp
&&L_filter_int_is_neg, // 46
&&L_filter_int_eq,     // 47
```

And update `&&L_halt` index from 45 to 47.

- [ ] **Step 4: Verify compilation**

Run: `cmake --build build 2>&1 | tail -20`
Expected: Compiles without errors (new opcodes are defined but not yet used).

---

## Task 2: Add filter parsing for `is_neg` and `eq(n)`

**Files:**
- Modify: `include/injamm/parse.hpp:104-124` (parse_int_filter)

- [ ] **Step 1: Add `is_neg` and `eq` parsing to `parse_int_filter`**

In `parse_int_filter()`, add handling for `is_neg` and `eq(n)`:

```cpp
[[nodiscard]] constexpr std::optional<int_filter_entry> parse_int_filter(std::string_view name) noexcept {
  // 引数付きフィルタの処理
  auto paren = name.find('(');
  if (paren != std::string_view::npos && name.back() == ')') {
    auto fname = name.substr(0, paren);
    auto arg_str = name.substr(paren + 1, name.size() - paren - 2);
    int arg = 0;
    for (auto c : arg_str) {
      if (c >= '0' && c <= '9') arg = arg * 10 + (c - '0');
    }
    if (fname == "mod") return int_filter_entry{int_filter::mod, arg};
    if (fname == "eq") return int_filter_entry{int_filter::eq, arg};
  }
  // 引数なしフィルタ
  if (name == "abs") return int_filter_entry{int_filter::abs, 0};
  if (name == "hex") return int_filter_entry{int_filter::hex, 0};
  if (name == "oct") return int_filter_entry{int_filter::oct, 0};
  if (name == "bin") return int_filter_entry{int_filter::bin, 0};
  if (name == "neg") return int_filter_entry{int_filter::neg, 0};
  if (name == "numify") return int_filter_entry{int_filter::numify, 0};
  if (name == "is_neg") return int_filter_entry{int_filter::is_neg, 0};
  return std::nullopt;
}
```

- [ ] **Step 2: Verify compilation**

Run: `cmake --build build 2>&1 | tail -20`
Expected: Compiles without errors.

---

## Task 3: Extend `chunk_if` to store filter information

**Files:**
- Modify: `include/injamm/chunk.hpp:97-101` (chunk_if struct)

- [ ] **Step 1: Add filter vectors to `chunk_if`**

```cpp
struct chunk_if {
  std::string expr;
  std::vector<string_filter_entry> filters;
  std::vector<int_filter_entry> int_filters;
  std::vector<float_filter_entry> float_filters;
  std::vector<parsed_template> then_branch;
  std::vector<parsed_template> else_branch;
};
```

- [ ] **Step 2: Verify compilation**

Run: `cmake --build build 2>&1 | tail -20`
Expected: Compiles without errors.

---

## Task 4: Extend `ct_parsed_template` and `ct_parse_context` for if-filters

**Files:**
- Modify: `include/injamm/ct_chunk.hpp:174-186` (push_if method)
- Modify: `include/injamm/ct_parse.hpp:107-112` (push_if method)
- Modify: `include/injamm/ct_parse.hpp:122-128` (update_if method)

- [ ] **Step 1: Extend `ct_parsed_template::push_if` to accept filters**

```cpp
constexpr void push_if(std::string_view expr, std::size_t then_start, std::size_t then_end,
                        std::size_t else_start, std::size_t else_end,
                        std::vector<string_filter_entry> filter_list = {},
                        std::vector<int_filter_entry> int_filter_list = {},
                        std::vector<float_filter_entry> float_filter_list = {}) {
  if (size >= N) {
    throw std::overflow_error("ct_parsed_template: chunk buffer overflow");
  }
  kinds[size] = ct_chunk_kind::if_else;
  texts[size] = expr;
  body_starts[size] = then_start;
  body_ends[size] = then_end;
  else_starts[size] = else_start;
  else_ends[size] = else_end;
  filters[size] = std::move(filter_list);
  int_filters[size] = std::move(int_filter_list);
  float_filters[size] = std::move(float_filter_list);
  ++size;
}
```

- [ ] **Step 2: Extend `ct_parse_context::push_if` to accept filters**

```cpp
constexpr std::size_t push_if(std::string_view expr, std::size_t then_start, std::size_t then_end,
                               std::size_t else_start, std::size_t else_end,
                               std::vector<string_filter_entry> filter_list = {},
                               std::vector<int_filter_entry> int_filter_list = {},
                               std::vector<float_filter_entry> float_filter_list = {}) {
  auto idx = tmpl.size;
  tmpl.push_if(expr, then_start, then_end, else_start, else_end,
               std::move(filter_list), std::move(int_filter_list), std::move(float_filter_list));
  return idx;
}
```

- [ ] **Step 3: Verify compilation**

Run: `cmake --build build 2>&1 | tail -20`
Expected: Compiles without errors.

---

## Task 5: Modify runtime parser to handle filter chains in if-expressions

**Files:**
- Modify: `include/injamm/parse.hpp:316-374` (parse_into, if block handling)

- [ ] **Step 1: Parse filter chains from if-expressions**

Replace the if-block handling in `parse_into` (lines 316-374). The key change is splitting `expr` by `|` and parsing filters:

```cpp
/** {{#if X}} — if ブロック */
if (key.starts_with("if") && (key.size() == 2 || key[2] == ' ')) {
  auto expr_raw = key.size() > 2 ? trim_sv(key.substr(3)) : std::string_view{};

  /** フィルタチェーンの解析: "age | is_neg" → key="age", filters=[is_neg] */
  auto parts = split_by_pipe(expr_raw);
  auto expr = parts.empty() ? std::string_view{} : parts[0];
  std::vector<string_filter_entry> if_filters;
  std::vector<int_filter_entry> if_int_filters;
  std::vector<float_filter_entry> if_float_filters;
  for (std::size_t fi = 1; fi < parts.size(); ++fi) {
    auto sf = parse_string_filter(parts[fi]);
    if (sf) {
      if_filters.push_back(*sf);
      continue;
    }
    auto ifl = parse_int_filter(parts[fi]);
    if (ifl) {
      if_int_filters.push_back(*ifl);
      continue;
    }
    auto ffl = parse_float_filter(parts[fi]);
    if (ffl) {
      if_float_filters.push_back(*ffl);
      continue;
    }
    result.push_back(chunk_literal{std::string{"ERROR_UNKNOWN_FILTER"}});
    pos = end + 3;  // skip past the closing tag area
    // Note: we need to handle the position correctly here
    // For simplicity, we emit the error and skip the rest of the if block
    goto if_block_done;
  }

  // ... rest of the existing if block parsing (depth counting, body extraction, etc.)
  // At the end, when creating chunk_if:
  chunk_if ci;
  ci.expr = std::string{expr};
  ci.filters = std::move(if_filters);
  ci.int_filters = std::move(if_int_filters);
  ci.float_filters = std::move(if_float_filters);
  ci.then_branch = wrap_body_chunks(parse(then_body));
  ci.else_branch = wrap_body_chunks(parse(else_body));
  result.push_back(std::move(ci));
  continue;
}
if_block_done:;
```

**Implementation note:** Parse filters first into local vectors, then proceed with the existing depth-counting / body-extraction logic unchanged. At the end, populate `chunk_if` with the parsed filters. If an unknown filter is encountered, emit `ERROR_UNKNOWN_FILTER` and `continue` (skip the rest of this if block — the existing error handling in the placeholder parser uses `return` but for if-blocks `continue` is appropriate since we're in a while loop).

- [ ] **Step 2: Verify compilation**

Run: `cmake --build build 2>&1 | tail -20`
Expected: Compiles without errors.

---

## Task 6: Modify compile-time parser to handle filter chains in if-expressions

**Files:**
- Modify: `include/injamm/ct_parse.hpp:274-347` (ct_parse_into, if block handling)

- [ ] **Step 1: Parse filter chains and pass to push_if**

In `ct_parse_into`, replace the if block handling. The change mirrors the runtime parser:

```cpp
if (key.starts_with("if") && (key.size() == 2 || key[2] == ' ')) {
  auto expr_raw = key.size() > 2 ? trim_sv(key.substr(3)) : std::string_view{};

  /** フィルタチェーンの解析 */
  auto parts = split_by_pipe(expr_raw);
  auto expr = parts.empty() ? std::string_view{} : parts[0];
  std::vector<string_filter_entry> if_filters;
  std::vector<int_filter_entry> if_int_filters;
  std::vector<float_filter_entry> if_float_filters;
  for (std::size_t fi = 1; fi < parts.size(); ++fi) {
    auto sf = parse_string_filter(parts[fi]);
    if (sf) {
      if_filters.push_back(*sf);
      continue;
    }
    auto ifl = parse_int_filter(parts[fi]);
    if (ifl) {
      if_int_filters.push_back(*ifl);
      continue;
    }
    auto ffl = parse_float_filter(parts[fi]);
    if (ffl) {
      if_float_filters.push_back(*ffl);
      continue;
    }
  }

  // ... existing depth counting and body extraction ...

  auto chunk_idx = ctx.push_if(expr, 0, 0, 0, 0,
                                std::move(if_filters), std::move(if_int_filters), std::move(if_float_filters));

  // ... rest unchanged (then/else body parsing, update_if) ...
}
```

- [ ] **Step 2: Verify compilation**

Run: `cmake --build build 2>&1 | tail -20`
Expected: Compiles without errors.

---

## Task 7: Modify bytecode compiler to emit filter instructions for if

**Files:**
- Modify: `include/injamm/bytecode_compile.hpp:218-240` (compile_if)

- [ ] **Step 1: Parse filter chain in `compile_if` and emit filter instructions**

The `compile_if` method currently receives a bare `expr` string. We need to:
1. Parse filters from the expression (same split_by_pipe logic)
2. Store filters in the var_ref
3. Emit `resolve_filtered` + filter opcodes before `emit_if`

**However**, there's a design decision: `compile_if` currently receives `expr` as a `std::string_view`. We need to change the signature to also accept filter vectors, or parse them inside `compile_if`.

**Approach:** Change `compile_if` to accept the full expression string and parse filters internally. This mirrors what the runtime parser does.

```cpp
void compile_if(std::string_view expr_full) {
  /** フィルタチェーンの解析 */
  auto parts = split_by_pipe(expr_full);
  auto expr = parts.empty() ? std::string_view{} : parts[0];
  std::vector<string_filter_entry> filters;
  std::vector<int_filter_entry> int_filters;
  std::vector<float_filter_entry> float_filters;
  for (std::size_t fi = 1; fi < parts.size(); ++fi) {
    auto sf = parse_string_filter(parts[fi]);
    if (sf) { filters.push_back(*sf); continue; }
    auto ifl = parse_int_filter(parts[fi]);
    if (ifl) { int_filters.push_back(*ifl); continue; }
    auto ffl = parse_float_filter(parts[fi]);
    if (ffl) { float_filters.push_back(*ffl); continue; }
  }

  auto idx = bc_.add_var_ref(expr);
  auto field_idx = resolve_field_index<T>(expr);
  if (field_idx != UINT32_MAX) {
    bc_.set_field_index(idx, field_idx);
  }
  bc_.var_refs[idx].filters = filters;
  bc_.var_refs[idx].int_filters = int_filters;
  bc_.var_refs[idx].float_filters = float_filters;

  /** フィルタがある場合は resolve_filtered → filter_* 命令列を発行 */
  bool has_filters = !filters.empty() || !int_filters.empty() || !float_filters.empty();
  if (has_filters) {
    bc_.add_instruction(bc_opcode::resolve_filtered, 0, idx);
    for (auto f : filters) {
      switch (f.filter) {
        case string_filter::upper:      bc_.add_instruction(bc_opcode::filter_upper); break;
        case string_filter::lower:      bc_.add_instruction(bc_opcode::filter_lower); break;
        case string_filter::capitalize: bc_.add_instruction(bc_opcode::filter_capitalize); break;
        case string_filter::title:      bc_.add_instruction(bc_opcode::filter_title); break;
        case string_filter::trim:       bc_.add_instruction(bc_opcode::filter_trim); break;
        case string_filter::ltrim:      bc_.add_instruction(bc_opcode::filter_ltrim); break;
        case string_filter::rtrim:      bc_.add_instruction(bc_opcode::filter_rtrim); break;
        case string_filter::left:       bc_.add_instruction(bc_opcode::filter_left, f.arg1); break;
        case string_filter::right:      bc_.add_instruction(bc_opcode::filter_right, f.arg1); break;
        case string_filter::center:     bc_.add_instruction(bc_opcode::filter_center, f.arg1); break;
        case string_filter::truncate:   bc_.add_instruction(bc_opcode::filter_truncate, f.arg1); break;
        case string_filter::substr:     bc_.add_instruction(bc_opcode::filter_substr, f.arg1, f.arg2); break;
      }
    }
    for (auto f : int_filters) {
      switch (f.filter) {
        case int_filter::abs:    bc_.add_instruction(bc_opcode::filter_int_abs); break;
        case int_filter::hex:    bc_.add_instruction(bc_opcode::filter_int_hex); break;
        case int_filter::oct:    bc_.add_instruction(bc_opcode::filter_int_oct); break;
        case int_filter::bin:    bc_.add_instruction(bc_opcode::filter_int_bin); break;
        case int_filter::neg:    bc_.add_instruction(bc_opcode::filter_int_neg); break;
        case int_filter::mod:    bc_.add_instruction(bc_opcode::filter_int_mod, f.arg); break;
        case int_filter::numify: bc_.add_instruction(bc_opcode::filter_int_numify); break;
        case int_filter::is_neg: bc_.add_instruction(bc_opcode::filter_int_is_neg); break;
        case int_filter::eq:     bc_.add_instruction(bc_opcode::filter_int_eq, f.arg); break;
      }
    }
    for (auto f : float_filters) {
      switch (f.filter) {
        case float_filter::precision: bc_.add_instruction(bc_opcode::filter_float_precision, f.arg); break;
      }
    }
  }

  bc_.add_instruction(bc_opcode::emit_if, 0, idx);

  auto if_instr_idx = bc_.current_offset() - 1;

  std::uint32_t else_instr_idx = 0;
  bool has_else = compile_body_with_else(else_instr_idx);

  if (has_else) {
    bc_.add_instruction(bc_opcode::emit_endif);
    bc_.patch_jump(if_instr_idx, else_instr_idx + 1);
    bc_.patch_jump(else_instr_idx, static_cast<std::uint32_t>(bc_.current_offset()));
  } else {
    bc_.add_instruction(bc_opcode::emit_endif);
    bc_.patch_jump(if_instr_idx, static_cast<std::uint32_t>(bc_.current_offset()));
  }
}
```

- [ ] **Step 2: Update call sites of `compile_if`**

In `compile_body()` and `compile_body_with_else()`, the call to `compile_if(expr)` already passes the expression string. Since `compile_if` now parses filters internally, no change is needed at call sites.

- [ ] **Step 3: Verify compilation**

Run: `cmake --build build 2>&1 | tail -20`
Expected: Compiles without errors.

---

## Task 8: Add new opcode handlers in bytecode VM

**Files:**
- Modify: `include/injamm/bytecode_exec.hpp` (computed goto section + switch section)

- [ ] **Step 1: Add `L_filter_int_is_neg` handler (computed goto)**

Add after `L_filter_int_numify`:

```cpp
/** @brief 負数判定: 値が負なら "true"、そうでなければ "false" を出力する */
L_filter_int_is_neg: {
  try {
    if (filtered_value_.find('.') != std::string::npos || filtered_value_.find('e') != std::string::npos || filtered_value_.find('E') != std::string::npos) {
      double val = std::stod(filtered_value_);
      filtered_value_ = val < 0 ? "true" : "false";
    } else {
      long long val = std::stoll(filtered_value_);
      filtered_value_ = val < 0 ? "true" : "false";
    }
  } catch (...) {
    filtered_value_ = "false";
  }
  ++pc;
  DISPATCH();
}
```

- [ ] **Step 2: Add `L_filter_int_eq` handler (computed goto)**

```cpp
/** @brief 等価判定: 値と引数が等しけなら "true"、そうでなければ "false" を出力する */
L_filter_int_eq: {
  try {
    auto target = bc_.instructions[pc].operand;
    if (filtered_value_.find('.') != std::string::npos || filtered_value_.find('e') != std::string::npos || filtered_value_.find('E') != std::string::npos) {
      double val = std::stod(filtered_value_);
      filtered_value_ = (static_cast<long long>(val) == target) ? "true" : "false";
    } else {
      long long val = std::stoll(filtered_value_);
      filtered_value_ = (val == target) ? "true" : "false";
    }
  } catch (...) {
    filtered_value_ = "false";
  }
  ++pc;
  DISPATCH();
}
```

- [ ] **Step 3: Add switch-case handlers for the new opcodes**

Add in the switch section:

```cpp
case bc_opcode::filter_int_is_neg: {
  try {
    if (filtered_value_.find('.') != std::string::npos || filtered_value_.find('e') != std::string::npos || filtered_value_.find('E') != std::string::npos) {
      double val = std::stod(filtered_value_);
      filtered_value_ = val < 0 ? "true" : "false";
    } else {
      long long val = std::stoll(filtered_value_);
      filtered_value_ = val < 0 ? "true" : "false";
    }
  } catch (...) {
    filtered_value_ = "false";
  }
  ++pc;
  break;
}

case bc_opcode::filter_int_eq: {
  try {
    auto target = instr.operand;
    if (filtered_value_.find('.') != std::string::npos || filtered_value_.find('e') != std::string::npos || filtered_value_.find('E') != std::string::npos) {
      double val = std::stod(filtered_value_);
      filtered_value_ = (static_cast<long long>(val) == target) ? "true" : "false";
    } else {
      long long val = std::stoll(filtered_value_);
      filtered_value_ = (val == target) ? "true" : "false";
    }
  } catch (...) {
    filtered_value_ = "false";
  }
  ++pc;
  break;
}
```

- [ ] **Step 4: Modify `L_emit_if` to apply filters before truthiness check**

In the `L_emit_if` handler (both computed goto and switch versions), after resolving the var_ref and before checking truthiness, apply filters if present:

```cpp
L_emit_if: {
  auto const& instr = bc_.instructions[pc];
  auto const& ref = bc_.var_refs[instr.operand2];
  bool cond = false;

  /** フィルタが設定されている場合は filtered_value_ を使用 */
  bool has_filters = !ref.filters.empty() || !ref.int_filters.empty() || !ref.float_filters.empty();

  if (has_filters) {
    /** フィルタ適用済みの filtered_value_ で真偽判定 */
    cond = !filtered_value_.empty() && filtered_value_ != "false" && filtered_value_ != "0";
  } else if (ref.key.starts_with("@")) {
    // ... existing @-prefix handling (unchanged) ...
  } else {
    // ... existing for_each_field truthiness check (unchanged) ...
  }

  if (!cond) {
    pc = instr.operand;
  } else {
    ++pc;
  }
  DISPATCH();
}
```

**Key insight:** When filters are present, the `resolve_filtered` + filter opcodes have already run before `emit_if` and populated `filtered_value_`. So `emit_if` just needs to check `filtered_value_` for truthiness instead of doing `for_each_field`.

- [ ] **Step 5: Verify compilation**

Run: `cmake --build build 2>&1 | tail -20`
Expected: Compiles without errors.

---

## Task 9: Modify compile-time renderer to apply filters in if-conditions

**Files:**
- Modify: `include/injamm/ct_render.hpp:803-843` (ct_render_if)

- [ ] **Step 1: Apply filters before truthiness check in `ct_render_if`**

```cpp
template <class Mode, class Buffer, class T, class RootT, std::size_t N>
constexpr auto ct_render_if(Buffer& out, ct_parsed_template<N> const& chunks, std::size_t i,
                             T const& value, RootT const& root_value, loop_state const* loop)
    -> std::expected<void, error_ctx> {
  bool cond = false;
  auto const expr = chunks.texts[i];

  auto const& filters = chunks.filters[i];
  auto const& int_filters = chunks.int_filters[i];
  auto const& float_filters = chunks.float_filters[i];

  /** フィルタがある場合は先に値を解決してフィルタを適用 */
  if (!filters.empty() || !int_filters.empty() || !float_filters.empty()) {
    std::string tmp;
    if (resolve_value(tmp, expr, value, loop)) {
      for (auto f : filters) {
        apply_string_filter(tmp, f);
      }
      for (auto f : int_filters) {
        apply_int_filter(tmp, f);
      }
      for (auto f : float_filters) {
        apply_float_filter(tmp, f);
      }
      cond = !tmp.empty() && tmp != "false" && tmp != "0";
    }
  } else if (!expr.empty() && expr[0] == '@') {
    /** @prefix の特殊変数を判定 */
    if (loop) {
      if (expr == "@last") cond = loop->is_last();
      else if (expr == "@first") cond = loop->is_first();
      else if (expr == "@index") cond = loop->index > 0;
    }
  } else {
    /** 通常の文字列条件式の解決 */
    std::string tmp;
    if (resolve_value(tmp, expr, value, loop)) {
      cond = !tmp.empty() && tmp != "false" && tmp != "0";
    }
  }

  auto then_start = chunks.body_starts[i];
  auto then_end = chunks.body_ends[i];
  auto else_start = chunks.else_starts[i];
  auto else_end = chunks.else_ends[i];

  auto [start, end] = cond ? std::pair{then_start, then_end} : std::pair{else_start, else_end};
  auto r = ct_render_chunks<Mode>(out, chunks, start, end, value, root_value, loop);
  if (!r) return r;
  return {};
}
```

- [ ] **Step 2: Verify compilation**

Run: `cmake --build build 2>&1 | tail -20`
Expected: Compiles without errors.

---

## Task 10: Add filter-in-if test cases

**Files:**
- Modify: `tests/test_injamm.cpp`

- [ ] **Step 1: Add test data type for if-filter tests**

```cpp
struct BcFilterIfData {
  int age{};
  int score{};
};

template <>
struct glz::meta<BcFilterIfData> {
  static constexpr auto value = glz::object("age", &BcFilterIfData::age, "score", &BcFilterIfData::score);
};
```

- [ ] **Step 2: Add `{{#if age | is_neg}}` tests**

```cpp
TEST_CASE("if_filter: is_neg true", "[if_filter]") {
  auto bc = injamm::engine<BcFilterIfData>("{{#if age | is_neg}}negative{{/if}}");
  BcFilterIfData data{.age = -5};
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "negative");
}

TEST_CASE("if_filter: is_neg false", "[if_filter]") {
  auto bc = injamm::engine<BcFilterIfData>("{{#if age | is_neg}}negative{{/if}}");
  BcFilterIfData data{.age = 5};
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "");
}

TEST_CASE("if_filter: is_neg zero", "[if_filter]") {
  auto bc = injamm::engine<BcFilterIfData>("{{#if age | is_neg}}negative{{/if}}");
  BcFilterIfData data{.age = 0};
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "");
}
```

- [ ] **Step 3: Add `{{#if age | eq(5)}}` tests**

```cpp
TEST_CASE("if_filter: eq true", "[if_filter]") {
  auto bc = injamm::engine<BcFilterIfData>("{{#if age | eq(5)}}five{{/if}}");
  BcFilterIfData data{.age = 5};
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "five");
}

TEST_CASE("if_filter: eq false", "[if_filter]") {
  auto bc = injamm::engine<BcFilterIfData>("{{#if age | eq(5)}}five{{/if}}");
  BcFilterIfData data{.age = 10};
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "");
}
```

- [ ] **Step 4: Add filter chain in if tests**

```cpp
TEST_CASE("if_filter: mod then eq", "[if_filter]") {
  auto bc = injamm::engine<BcFilterIfData>("{{#if score | mod(4) | eq(2)}}match{{/if}}");
  BcFilterIfData data{.score = 6};
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "match");
}

TEST_CASE("if_filter: mod then eq no match", "[if_filter]") {
  auto bc = injamm::engine<BcFilterIfData>("{{#if score | mod(4) | eq(2)}}match{{/if}}");
  BcFilterIfData data{.score = 5};
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "");
}
```

- [ ] **Step 5: Add if/else with filter tests**

```cpp
TEST_CASE("if_filter: is_neg with else", "[if_filter]") {
  auto bc = injamm::engine<BcFilterIfData>("{{#if age | is_neg}}neg{{else}}pos{{/if}}");
  BcFilterIfData data{.age = -3};
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "neg");
}

TEST_CASE("if_filter: is_neg with else false branch", "[if_filter]") {
  auto bc = injamm::engine<BcFilterIfData>("{{#if age | is_neg}}neg{{else}}pos{{/if}}");
  BcFilterIfData data{.age = 3};
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "pos");
}
```

- [ ] **Step 6: Add backward compatibility test**

```cpp
TEST_CASE("if_filter: plain if still works", "[if_filter]") {
  auto bc = injamm::engine<BcFilterIfData>("{{#if age}}nonzero{{/if}}");
  BcFilterIfData data{.age = 42};
  auto r = bc.render(data);
  REQUIRE(r.has_value());
  REQUIRE(*r == "nonzero");
}
```

- [ ] **Step 7: Build and run tests**

```bash
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=~/vm/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build
ctest --test-dir build -V -R "if_filter"
```

Expected: All new tests pass, all existing tests still pass.

---

## Edge Cases Handled

| Case | Behavior |
|------|----------|
| `{{#if}}` with no expression | `expr` is empty string, resolves to falsy → always false (existing behavior) |
| `{{#if @last}}` with filters | Filters are parsed but `@`-prefix check happens first; filters are effectively ignored for `@`-prefix vars (consistent with `@` vars being special) |
| Filter chain producing non-boolean | The result string is checked for truthiness (empty/"false"/"0" = false, everything else = true) |
| Interaction with `{{else}}` | Works identically — filters affect the condition, not the branching structure |
| Unknown filter in if | Emits `ERROR_UNKNOWN_FILTER` literal and continues parsing (existing error behavior) |
| `{{#if | is_neg}}` (no expr) | `parts[0]` is empty, `resolve_value` fails → condition is false |

## Implementation Order Summary

1. **Task 1**: Enum/opcode additions (no behavior change)
2. **Task 2**: Filter parsing (no behavior change)
3. **Task 3**: AST extension (no behavior change)
4. **Task 4**: CT AST extension (no behavior change)
5. **Task 5**: Runtime parser (enables `chunk_if.filters`)
6. **Task 6**: CT parser (enables ct filter storage)
7. **Task 7**: Bytecode compiler (emits filter instructions)
8. **Task 8**: Bytecode VM (executes filter instructions + if with filters)
9. **Task 9**: CT renderer (applies filters in if)
10. **Task 10**: Tests (validates everything)
