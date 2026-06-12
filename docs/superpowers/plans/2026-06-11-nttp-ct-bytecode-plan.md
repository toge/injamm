# NTTP CT Bytecode + VM Executor Integration — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace `ct_render_chunks` (function-call dispatch, slow) with CT bytecode generation + VM executor (computed goto, fast) for `render<fixed_string>()`.

**Architecture:** `ct_parse_into()` → `ct_chunks_to_bytecode<T>()` → `ct_bytecode<N>` → `to_bytecode()` → `bytecode` (runtime) → `bc_execute()`.

**Tech Stack:** C++23, constexpr, glaze reflection, computed goto

---

### File Structure

| File | Action | Lines |
|------|--------|-------|
| `include/injamm/bytecode_ct_compile.hpp` | Create | ~450 |
| `include/injamm/escape_hatch.hpp` | Modify | ~10 lines changed |
| `include/injamm/ct_render.hpp` | Delete | ~980 |

### Task 1: Create `bytecode_ct_compile.hpp` with infrastructure types

**Files:**
- Create: `include/injamm/bytecode_ct_compile.hpp`

- [ ] **Step 1: Write the header with types, builder, `resolve_field_indices`, `to_bytecode`**

```cpp
#pragma once

#include "bytecode.hpp"
#include "bytecode_exec.hpp"
#include "ct_chunk.hpp"
#include "glz_dispatch.hpp"
#include "types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace injamm::detail {

struct string_ref {
  const char* data = nullptr;
  std::size_t size = 0;
  constexpr string_ref() = default;
  constexpr string_ref(const char* d, std::size_t s) : data(d), size(s) {}
};

struct ct_var_ref {
  string_ref key;
  std::uint32_t field_index = UINT32_MAX;
};

template <std::size_t N>
struct ct_bytecode {
  std::array<bc_instruction, N> instructions{};
  std::array<string_ref, N> literals{};
  std::array<ct_var_ref, N> var_refs{};
  std::size_t instr_count{};
  std::size_t literal_count{};
  std::size_t var_ref_count{};
  error_ctx error{};
};

template <std::size_t N>
struct ct_bytecode_builder {
  ct_bytecode<N>& bc;

  std::uint32_t add_literal(string_ref lit) {
    auto idx = static_cast<std::uint32_t>(bc.literal_count);
    bc.literals[bc.literal_count] = lit;
    ++bc.literal_count;
    return idx;
  }

  std::uint32_t add_var_ref(string_ref key, std::uint32_t field_index) {
    auto idx = static_cast<std::uint32_t>(bc.var_ref_count);
    bc.var_refs[bc.var_ref_count] = {key, field_index};
    ++bc.var_ref_count;
    return idx;
  }

  void emit(bc_opcode op, std::uint32_t operand = 0, std::uint32_t operand2 = 0) {
    bc.instructions[bc.instr_count] = {op, operand, operand2};
    ++bc.instr_count;
  }

  std::size_t current_offset() const { return bc.instr_count; }

  void patch_jump(std::size_t idx, std::uint32_t target) {
    bc.instructions[idx].operand = target;
  }
};

// to_bytecode: ct_bytecode<N> → runtime bytecode
template <std::size_t N>
bytecode to_bytecode(ct_bytecode<N> const& ct) {
  bytecode bc;
  bc.instructions.assign(ct.instructions.begin(), ct.instructions.begin() + ct.instr_count);
  bc.literals.reserve(ct.literal_count);
  for (std::size_t i = 0; i < ct.literal_count; ++i)
    bc.literals.emplace_back(ct.literals[i].data, ct.literals[i].size);
  bc.var_refs.reserve(ct.var_ref_count);
  for (std::size_t i = 0; i < ct.var_ref_count; ++i) {
    bc_var_ref ref;
    ref.key.assign(ct.var_refs[i].key.data, ct.var_refs[i].key.size);
    ref.field_index = ct.var_refs[i].field_index;
    bc.var_refs.push_back(std::move(ref));
  }
  bc.error = ct.error;
  return bc;
}

} // namespace injamm::detail
```

- [ ] **Step 2: Build to verify compilation**

Run: `cmake --build build`

- [ ] **Step 3: Commit**

```bash
git add include/injamm/bytecode_ct_compile.hpp
git commit -m "feat: add ct_bytecode, string_ref, builder, to_bytecode"
```

### Task 2: Wire `render<>()` to use CT bytecode path with stub

**Files:**
- Modify: `include/injamm/escape_hatch.hpp`
- Modify: `include/injamm/bytecode_ct_compile.hpp`

- [ ] **Step 1: Replace include in `escape_hatch.hpp`**

Change line 17 from `#include "ct_render.hpp"` to `#include "bytecode_ct_compile.hpp"`.

- [ ] **Step 2: Rewrite both `render<>()` overloads**

First overload (was lines 195-205):
```cpp
template <injamm::fixed_string Tmpl, typename T>
[[nodiscard]] expected<std::string> render(T const& value) {
  constexpr auto parsed = detail::parse_fixed_impl<Tmpl>();
  if (parsed.error.ec != error_code::none)
    return std::unexpected(parsed.error);
  constexpr auto resolved = detail::resolve_field_indices<T>(parsed);
  constexpr auto ct_bc = detail::ct_chunks_to_bytecode<T>(resolved);
  if (ct_bc.error.ec != error_code::none)
    return std::unexpected(ct_bc.error);
  auto bc = detail::to_bytecode(ct_bc);
  return detail::bc_execute(bc, value);
}
```

Second overload (was lines 221-239):
```cpp
template <injamm::fixed_string Tmpl, injamm::fixed_string... Entries, typename T>
  requires(sizeof...(Entries) % 2 == 0)
[[nodiscard]] expected<std::string> render(T const& value) {
  constexpr auto ctx = detail::ct_expanded_template<Tmpl, Entries...>();
  if (ctx.error.ec != error_code::none)
    return std::unexpected(ctx.error);
  constexpr auto resolved = detail::resolve_field_indices<T>(ctx.tmpl);
  constexpr auto ct_bc = detail::ct_chunks_to_bytecode<T>(resolved);
  if (ct_bc.error.ec != error_code::none)
    return std::unexpected(ct_bc.error);
  auto bc = detail::to_bytecode(ct_bc);
  return detail::bc_execute(bc, value);
}
```

Note: This removes the `out.reserve(Tmpl.size() * 2)` line — `bc_execute` does its own `out.reserve(256)`. This is less optimized but will be addressed later if needed.

- [ ] **Step 3: Add `resolve_field_indices` and stub `ct_chunks_to_bytecode` to `bytecode_ct_compile.hpp`**

Add inside `namespace injamm::detail`, after `to_bytecode`:

```cpp
template <class T, std::size_t N>
constexpr ct_parsed_template<N> resolve_field_indices(ct_parsed_template<N> tmpl) {
  tmpl.field_indices.fill(-1);
  if constexpr (glz_reflectable<T>) {
    constexpr auto count = glz::reflect<T>::size;
    for (std::size_t i = 0; i < tmpl.size; ++i) {
      auto& idx = tmpl.field_indices[i];
      auto kind = tmpl.kinds[i];
      if (kind != ct_chunk_kind::placeholder && kind != ct_chunk_kind::section &&
          kind != ct_chunk_kind::inverted && kind != ct_chunk_kind::if_else)
        continue;
      auto key = tmpl.texts[i];
      if (key.empty() || key[0] == '@' || key.find('.') != std::string_view::npos)
        continue;
      [&]<std::size_t... I>(std::index_sequence<I...>) {
        ((std::string_view{glz::reflect<T>::keys[I]} == key && (idx = static_cast<int>(I), true)) || ...);
      }(std::make_index_sequence<count>{});
    }
  }
  return tmpl;
}

template <std::size_t N>
consteval void compile_chunk_range(ct_bytecode_builder<N>& b,
                                   ct_parsed_template<N> const& chunks,
                                   std::size_t start, std::size_t end);

template <class T, std::size_t N>
consteval ct_bytecode<N> ct_chunks_to_bytecode(ct_parsed_template<N> const& chunks) {
  ct_bytecode<N> bc;
  ct_bytecode_builder<N> b{bc};
  compile_chunk_range(b, chunks, 0, chunks.size);
  return bc;
}

template <std::size_t N>
consteval void compile_chunk_range(ct_bytecode_builder<N>& b,
                                   ct_parsed_template<N> const& chunks,
                                   std::size_t start, std::size_t end) {
  for (std::size_t i = start; i < end; ++i) {
    switch (chunks.kinds[i]) {
    default: break;
    }
  }
}
```

- [ ] **Step 4: Build and run tests**

Run: `cmake --build build && ./build/injamm_tests`
Expected: NTTP tests fail (stub produces empty output). Non-NTTP tests pass.

- [ ] **Step 5: Commit**

```bash
git add include/injamm/escape_hatch.hpp include/injamm/bytecode_ct_compile.hpp
git commit -m "feat: wire ct_chunks_to_bytecode into render<> (stub)"
```

### Task 3: Implement `literal` chunk → `emit_literal`

**Files:**
- Modify: `include/injamm/bytecode_ct_compile.hpp`

- [ ] **Step 1: Add `literal` case before `default`**

```cpp
case ct_chunk_kind::literal: {
  auto lit_idx = b.add_literal({chunks.texts[i].data(), chunks.texts[i].size()});
  b.emit(bc_opcode::emit_literal, lit_idx);
  break;
}
```

- [ ] **Step 2: Build and run tests**

Run: `cmake --build build && ./build/injamm_tests`
Expected: Tests with only literal text pass. Variable/section tests still fail.

- [ ] **Step 3: Commit**

```bash
git add include/injamm/bytecode_ct_compile.hpp
git commit -m "feat: compile literal chunks to emit_literal"
```

### Task 4: Implement `placeholder` → variable output (no filters)

**Files:**
- Modify: `include/injamm/bytecode_ct_compile.hpp`

- [ ] **Step 1: Add `placeholder` case before `default`**

Two sub-cases:
- `@root.field` (text starts with `@root.`) → `emit_at_root_field`/`emit_at_root_field_raw`. Strip `@root.` prefix for var_ref key (same as `bc_compiler::emit_root_field` line 140: `key.substr(6)`).
- Normal variable → `emit_var`/`emit_var_raw`. If preceding instruction is `emit_literal`, fuse into `emit_litvar`/`emit_litvar_raw`.

```cpp
case ct_chunk_kind::placeholder: {
  auto sv = chunks.texts[i];
  bool raw = (chunks.flags[i] != 0);
  bool has_filters = (chunks.filter_count[i] > 0 || chunks.int_filter_count[i] > 0
                      || chunks.float_filter_count[i] > 0);

  // @root.field → emit_at_root_field
  if (sv.starts_with("@root.")) {
    auto rest = sv.substr(6);
    auto vridx = b.add_var_ref({rest.data(), rest.size()},
                                static_cast<std::uint32_t>(chunks.field_indices[i]));
    b.emit(raw ? bc_opcode::emit_at_root_field_raw : bc_opcode::emit_at_root_field, 0, vridx);
    break;
  }

  auto vridx = b.add_var_ref({sv.data(), sv.size()},
                              static_cast<std::uint32_t>(chunks.field_indices[i]));

  // Fusion: preceding emit_literal + no-filters var → emit_litvar
  if (!has_filters && b.bc.instr_count > 0) {
    auto& prev = b.bc.instructions[b.bc.instr_count - 1];
    if (prev.op == bc_opcode::emit_literal) {
      prev.op = raw ? bc_opcode::emit_litvar_raw : bc_opcode::emit_litvar;
      prev.operand2 = vridx;
      break;
    }
  }

  b.emit(raw ? bc_opcode::emit_var_raw : bc_opcode::emit_var, 0, vridx);
  break;
}
```

- [ ] **Step 2: Build and run tests**

Run: `cmake --build build && ./build/injamm_tests`
Expected: Simple var tests pass (`{{name}}`, `{{{raw}}}`, `{{a}},{{b}}`, `{{@root.field}}`). Filter/section/if tests may still fail.

- [ ] **Step 3: Commit**

```bash
git add include/injamm/bytecode_ct_compile.hpp
git commit -m "feat: compile placeholder to emit_var/emit_litvar + @root.field"
```

### Task 5: Implement `section` + `inverted` → emit_section/emit_inverted

**Files:**
- Modify: `include/injamm/bytecode_ct_compile.hpp`

- [ ] **Step 1: Add section and inverted cases before `default`**

Backpatching convention (matching `bc_compiler`):
- `emit_section`/`emit_inverted` operand initially 0, patched to address past `emit_end`
- `emit_end` operand points back to the section start

```cpp
case ct_chunk_kind::section: {
  auto vridx = b.add_var_ref({chunks.texts[i].data(), chunks.texts[i].size()},
                              static_cast<std::uint32_t>(chunks.field_indices[i]));
  auto sec_instr = b.current_offset();
  b.emit(bc_opcode::emit_section, 0, vridx);
  compile_chunk_range(b, chunks, chunks.body_starts[i], chunks.body_ends[i]);
  b.emit(bc_opcode::emit_end, static_cast<std::uint32_t>(sec_instr));
  b.patch_jump(sec_instr, static_cast<std::uint32_t>(b.current_offset()));
  break;
}
case ct_chunk_kind::inverted: {
  auto vridx = b.add_var_ref({chunks.texts[i].data(), chunks.texts[i].size()},
                              static_cast<std::uint32_t>(chunks.field_indices[i]));
  auto inv_instr = b.current_offset();
  b.emit(bc_opcode::emit_inverted, 0, vridx);
  compile_chunk_range(b, chunks, chunks.body_starts[i], chunks.body_ends[i]);
  b.emit(bc_opcode::emit_end, static_cast<std::uint32_t>(inv_instr));
  b.patch_jump(inv_instr, static_cast<std::uint32_t>(b.current_offset()));
  break;
}
```

- [ ] **Step 2: Build and run tests**

Run: `cmake --build build && ./build/injamm_tests`
Expected: Section/inverted tests pass (`{{#items}}{{name}}{{/items}}`, `{{^empty}}displayed{{/empty}}`).

- [ ] **Step 3: Commit**

```bash
git add include/injamm/bytecode_ct_compile.hpp
git commit -m "feat: compile section/inverted to emit_section/emit_inverted"
```

### Task 6: Implement `at_var` + `at_section` → emit_at_*

**Files:**
- Modify: `include/injamm/bytecode_ct_compile.hpp`

- [ ] **Step 1: Add `at_var` case before `default`**

```cpp
case ct_chunk_kind::at_var: {
  auto ak = static_cast<ct_at_var_kind>(chunks.flags[i]);
  switch (ak) {
  case ct_at_var_kind::index: b.emit(bc_opcode::emit_at_index); break;
  case ct_at_var_kind::first: b.emit(bc_opcode::emit_at_first); break;
  case ct_at_var_kind::last:  b.emit(bc_opcode::emit_at_last); break;
  case ct_at_var_kind::key:   b.emit(bc_opcode::emit_at_key); break;
  case ct_at_var_kind::root:  b.emit(bc_opcode::emit_at_root); break;
  }
  break;
}
```

- [ ] **Step 2: Add `at_section` case before `default`**

```cpp
case ct_chunk_kind::at_section: {
  bool inverted = (chunks.else_starts[i] != 0); // inverted flag stored in else_starts by push_at_section
  auto body_op = inverted ? bc_opcode::emit_at_inverted : bc_opcode::emit_at_section;
  auto sec_instr = b.current_offset();
  // operand = flags[i] = ct_at_var_kind (index/first/last)
  b.emit(body_op, static_cast<std::uint32_t>(chunks.flags[i]));
  compile_chunk_range(b, chunks, chunks.body_starts[i], chunks.body_ends[i]);
  b.emit(bc_opcode::emit_end, static_cast<std::uint32_t>(sec_instr));
  b.patch_jump(sec_instr, static_cast<std::uint32_t>(b.current_offset()));
  break;
}
```

- [ ] **Step 3: Build and run tests**

Run: `cmake --build build && ./build/injamm_tests`
Expected: `@index`, `@first`, `@last`, `@key`, `@root`, `{{#@last}}`, `{{^@first}}` tests pass.

- [ ] **Step 4: Commit**

```bash
git add include/injamm/bytecode_ct_compile.hpp
git commit -m "feat: compile at_var/at_section to emit_at_index/emit_at_section etc."
```

### Task 7: Implement `if_else` → emit_if/emit_else/emit_endif (no filters)

**Files:**
- Modify: `include/injamm/bytecode_ct_compile.hpp`

- [ ] **Step 1: Add `if_else` case before `default`**

Backpatching matches `compile_body_with_else()`:
```
emit_if   operand=endif_addr, operand2=var_ref
  then_body...
emit_else operand=endif_addr       (if else clause exists)
  else_body...
emit_endif
```

```cpp
case ct_chunk_kind::if_else: {
  bool has_else = (chunks.else_starts[i] != 0 || chunks.else_ends[i] != 0);
  auto sv = chunks.texts[i];
  auto vridx = b.add_var_ref({sv.data(), sv.size()},
                              static_cast<std::uint32_t>(chunks.field_indices[i]));

  auto if_instr = b.current_offset();
  b.emit(bc_opcode::emit_if, 0, vridx);
  compile_chunk_range(b, chunks, chunks.body_starts[i], chunks.body_ends[i]);

  std::size_t endif_addr;
  if (has_else) {
    auto else_instr = b.current_offset();
    b.emit(bc_opcode::emit_else, 0);
    compile_chunk_range(b, chunks, chunks.else_starts[i], chunks.else_ends[i]);
    endif_addr = b.current_offset();
    b.emit(bc_opcode::emit_endif);
    b.patch_jump(else_instr, static_cast<std::uint32_t>(endif_addr + 1));
  } else {
    endif_addr = b.current_offset();
    b.emit(bc_opcode::emit_endif);
  }
  b.patch_jump(if_instr, static_cast<std::uint32_t>(endif_addr + 1));
  break;
}
```

- [ ] **Step 2: Build and run tests**

Run: `cmake --build build && ./build/injamm_tests`
Expected: if/else tests pass (`{{#if @last}}.{{else}},{{/if}}`).

- [ ] **Step 3: Commit**

```bash
git add include/injamm/bytecode_ct_compile.hpp
git commit -m "feat: compile if_else to emit_if/emit_else/emit_endif"
```

### Task 8: Implement filter chain + break/continue

**Files:**
- Modify: `include/injamm/bytecode_ct_compile.hpp`

- [ ] **Step 1: Add filter emission helper inside `compile_chunk_range`**

Add this at the top of `compile_chunk_range`, before the for loop:

```cpp
auto emit_filter_chain = [&](std::size_t idx) {
  for (std::uint8_t f = 0; f < chunks.filter_count[idx]; ++f) {
    auto const& sf = chunks.filters[idx][f];
    switch (sf.filter) {
    case string_filter::upper:      b.emit(bc_opcode::filter_upper); break;
    case string_filter::lower:      b.emit(bc_opcode::filter_lower); break;
    case string_filter::capitalize: b.emit(bc_opcode::filter_capitalize); break;
    case string_filter::title:      b.emit(bc_opcode::filter_title); break;
    case string_filter::trim:       b.emit(bc_opcode::filter_trim); break;
    case string_filter::ltrim:      b.emit(bc_opcode::filter_ltrim); break;
    case string_filter::rtrim:      b.emit(bc_opcode::filter_rtrim); break;
    case string_filter::left:       b.emit(bc_opcode::filter_left, sf.arg1); break;
    case string_filter::right:      b.emit(bc_opcode::filter_right, sf.arg1); break;
    case string_filter::center:     b.emit(bc_opcode::filter_center, sf.arg1); break;
    case string_filter::truncate:   b.emit(bc_opcode::filter_truncate, sf.arg1); break;
    case string_filter::substr:     b.emit(bc_opcode::filter_substr, sf.arg1, sf.arg2); break;
    }
  }
  for (std::uint8_t f = 0; f < chunks.int_filter_count[idx]; ++f) {
    auto const& intf = chunks.int_filters[idx][f];
    switch (intf.filter) {
    case int_filter::abs:      b.emit(bc_opcode::filter_int_abs); break;
    case int_filter::hex:      b.emit(bc_opcode::filter_int_hex); break;
    case int_filter::oct:      b.emit(bc_opcode::filter_int_oct); break;
    case int_filter::bin:      b.emit(bc_opcode::filter_int_bin); break;
    case int_filter::neg:      b.emit(bc_opcode::filter_int_neg); break;
    case int_filter::mod:      b.emit(bc_opcode::filter_int_mod, intf.arg); break;
    case int_filter::numify:   b.emit(bc_opcode::filter_int_numify); break;
    case int_filter::is_neg:   b.emit(bc_opcode::filter_int_is_neg); break;
    case int_filter::eq:       b.emit(bc_opcode::filter_int_eq, intf.arg); break;
    case int_filter::ne:       b.emit(bc_opcode::filter_int_ne, intf.arg); break;
    case int_filter::gt:       b.emit(bc_opcode::filter_int_gt, intf.arg); break;
    case int_filter::gte:      b.emit(bc_opcode::filter_int_gte, intf.arg); break;
    case int_filter::lt:       b.emit(bc_opcode::filter_int_lt, intf.arg); break;
    case int_filter::lte:      b.emit(bc_opcode::filter_int_lte, intf.arg); break;
    case int_filter::zerofill: b.emit(bc_opcode::filter_int_zerofill, intf.arg); break;
    }
  }
  for (std::uint8_t f = 0; f < chunks.float_filter_count[idx]; ++f) {
    auto const& ff = chunks.float_filters[idx][f];
    if (ff.filter == float_filter::precision)
      b.emit(bc_opcode::filter_float_precision, ff.arg);
  }
};
```

- [ ] **Step 2: Update `placeholder` case to use `emit_filter_chain` when `has_filters`**

Replace the simple `b.emit(emit_var/emit_var_raw)` in the placeholder case with filter-aware logic:

```cpp
case ct_chunk_kind::placeholder: {
  auto sv = chunks.texts[i];
  bool raw = (chunks.flags[i] != 0);
  bool has_filters = (chunks.filter_count[i] > 0 || chunks.int_filter_count[i] > 0
                      || chunks.float_filter_count[i] > 0);

  // @root.field + filters: resolve_filtered + filters + emit_filtered
  if (sv.starts_with("@root.")) {
    auto rest = sv.substr(6);
    auto vridx = b.add_var_ref({rest.data(), rest.size()},
                                static_cast<std::uint32_t>(chunks.field_indices[i]));
    if (has_filters) {
      b.emit(bc_opcode::resolve_filtered, 0, vridx);
      emit_filter_chain(i);
      b.emit(raw ? bc_opcode::emit_filtered_raw : bc_opcode::emit_filtered);
    } else {
      b.emit(raw ? bc_opcode::emit_at_root_field_raw : bc_opcode::emit_at_root_field, 0, vridx);
    }
    break;
  }

  auto vridx = b.add_var_ref({sv.data(), sv.size()},
                              static_cast<std::uint32_t>(chunks.field_indices[i]));

  if (has_filters) {
    b.emit(bc_opcode::resolve_filtered, 0, vridx);
    emit_filter_chain(i);
    b.emit(raw ? bc_opcode::emit_filtered_raw : bc_opcode::emit_filtered);
    break;
  }

  // Fusion with preceding emit_literal
  if (b.bc.instr_count > 0) {
    auto& prev = b.bc.instructions[b.bc.instr_count - 1];
    if (prev.op == bc_opcode::emit_literal) {
      prev.op = raw ? bc_opcode::emit_litvar_raw : bc_opcode::emit_litvar;
      prev.operand2 = vridx;
      break;
    }
  }

  b.emit(raw ? bc_opcode::emit_var_raw : bc_opcode::emit_var, 0, vridx);
  break;
}
```

Note: Unlike the runtime compiler, we do NOT fuse with filters. The runtime `emit_var` also doesn't fuse when filters are present (lines 74-84 check `filters.empty()` first, and the fusion only happens in the `filters.empty()` branch). So this matches.

- [ ] **Step 3: Add `ct_break` and `ct_continue` cases**

```cpp
case ct_chunk_kind::ct_break:
  b.emit(bc_opcode::emit_break);
  break;
case ct_chunk_kind::ct_continue:
  b.emit(bc_opcode::emit_continue);
  break;
```

- [ ] **Step 4: Build and run tests**

Run: `cmake --build build && ./build/injamm_tests`
Expected: All tests pass.

- [ ] **Step 5: Commit**

```bash
git add include/injamm/bytecode_ct_compile.hpp
git commit -m "feat: compile filters, break, continue to bytecode"
```

### Task 9: Delete `ct_render.hpp` and finalize

**Files:**
- Delete: `include/injamm/ct_render.hpp`
- Verify: `include/injamm/escape_hatch.hpp` no longer references `ct_render.hpp`

- [ ] **Step 1: Ensure `escape_hatch.hpp` only includes `bytecode_ct_compile.hpp`**

Double-check that `#include "ct_render.hpp"` has been replaced with `#include "bytecode_ct_compile.hpp"` and no other file depends on `ct_render.hpp`:

```bash
rg "ct_render.hpp" include/
```

If any other file includes it (e.g., test files), they need to be updated too. The test files should compile fine without it since the public API is via `escape_hatch.hpp`.

- [ ] **Step 2: Remove the file**

```bash
git rm include/injamm/ct_render.hpp
```

- [ ] **Step 3: Build and run full test suite**

```bash
cmake --build build && ./build/injamm_tests
```

Expected: All tests pass.

- [ ] **Step 4: Run benchmark to verify speedup**

```bash
cd ~/src/template-benchmark
cp ~/src/injamm/include/injamm/*.hpp include/injamm/
cmake --build build && ./build/bench_at_vars --benchmark_filter="nttp"
```

Expected: NTTP benchmarks show significant improvement (from ~400ns to ~50-100ns range).

- [ ] **Step 5: Commit**

```bash
git rm include/injamm/ct_render.hpp
git add include/injamm/bytecode_ct_compile.hpp include/injamm/escape_hatch.hpp
git commit -m "feat: remove ct_render.hpp, NTTP now uses CT bytecode + VM executor"
```
