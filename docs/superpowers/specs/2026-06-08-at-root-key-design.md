# @root and @key Support for injamm

## Overview

Add `@root` (root context access) and `@key` (map/struct field iteration key) support to injamm's Bytecode VM, complementing existing `@index`/`@first`/`@last`. Also add struct-field iteration (map-like iteration over `glz_reflectable` types) to make `@key` useful.

## 1. @root

### Problem

- `{{@root}}`: Bytecode VM silently ignores it (`bc_compiler::emit_at_var` default case breaks)
- `{{@root.field}}`: parser creates `chunk_at_var{kind::root}` losing the field path; Bytecode VM ignores it
- NTTP compile-time path works correctly via `ct_render_placeholder`

### Solution

#### Parser changes (`parse.hpp`, `ct_parse.hpp`)

Add special handling before the generic `@`-prefixed block:

| Input | Output |
|-------|--------|
| `@root` | `chunk_at_var{kind::root}` (at_var) |
| `@root.field` | `chunk_placeholder{"@root.field"}` (placeholder) |
| `@root.field.nested` | `chunk_placeholder{"@root.field.nested"}` (placeholder) |
| other `@var` | `chunk_at_var{parse_at_kind(var)}` (unchanged) |

This way `@root.field` flows through the placeholder path where both NTTP (`ct_render_placeholder`) and Bytecode VM (`bc_compiler::emit_var`) can handle it.

#### Bytecode opcodes (`bytecode.hpp`)

Add:
- `emit_at_root` (opcode 18) — serialize `root_value_`
- `emit_at_root_field` (opcode 19) — resolve path var_ref against `root_value_`
- `emit_at_root_field_raw` (opcode 20) — same but no HTML escaping

#### Bytecode compiler (`bytecode_compile.hpp`)

- `emit_at_var()`: handle `kind::root` → emit `emit_at_root`
- `compile_body()`: before calling `emit_var(key)`, check if key starts with `@root.` → strip prefix, add var_ref with remaining path, emit `emit_at_root_field` (or `emit_at_root_field_raw` for raw)

#### Bytecode executor (`bytecode_exec.hpp`)

Add handlers:
- `L_emit_at_root`: `serialize_value(out, root_value_)`
- `L_emit_at_root_field`: `for_each_field(root_value_, ref.key, ref.field_index, visitor)` with `emit_var_value`
- `L_emit_at_root_field_raw`: same but raw=true

Both computed-goto dispatch table and switch fallback updated.

#### NTTP path

No changes needed — `ct_render_placeholder` already handles `@root` and `@root.field` correctly via `stencil_tag` mode.

#### resolve.hpp

Add `@root` and `@root.field` handling to `resolve_value()` for the NTTP if/placeholder fallback path:

```cpp
if (key == "@root") { serialize_value(out, root_value); return true; }
if (key.starts_with("@root.")) { return resolve_value(out, key.substr(6), root_value, nullptr); }
```

## 2. @key + Struct-Field Iteration

### Problem

- `@key` doesn't exist
- Sections only iterate `vector-like` (arrays) or `bool`. `glz_reflectable` struct-typed fields are silently ignored
- No way to iterate over struct fields as key-value pairs

### Solution

#### New enum values

- `chunk_at_var::kind::key` (4th value)
- `ct_at_var_kind::key` (4th value)

#### Loop state extension

- `loop_state::key` (`std::string_view`, default empty)
- `bc_loop_state::key` (`std::string_view`, default empty)

#### Bytecode opcode

- `emit_at_key` (opcode 21) — serialize `loop_->key`

#### Section iteration: struct-field branch

In both `bc_executor::L_emit_section` and `ct_render_section`, add a third branch after `vector-like` and `bool`:

```cpp
else if constexpr (ct_glz_reflectable<FT>) {
    // Iterate over FT's reflected fields
    constexpr auto sz = glz::reflect<FT>::size;
    auto tied = glz::to_tie(field);
    [&]<std::size_t... I>(std::index_sequence<I...>) {
        loop_state ls; // or bc_loop_state
        ls.count = sz;
        ([&] {
            ls.key = glz::reflect<FT>::keys[I];
            // Set child context to glz::get<I>(tied)
            // Execute section body with child context
        }(), ...);
    }(std::make_index_sequence<sz>{});
}
```

This fold-expression iterates at compile time over all fields. Each field gets its own iteration with:
- `@key` = field name string
- `@index` = field position (0-based)
- `@first` / `@last` = position booleans
- child context = field value

The child executor type differs per field, handled naturally by the fold expression.

#### Parser

No parser changes needed — `{{#struct_field}}...{{/struct_field}}` already creates a `chunk_section`. The new behavior activates at render time based on the field's type.

### Performance

- Existing `vector-like` / `bool` paths: **zero overhead** (unchanged by `if constexpr`)
- Struct-field iteration: only compiles for `glz_reflectable` types that are not vector-like and not bool
- `@key` output: trivial `out_.append(loop_->key)` call
- All changes are compile-time dispatch; no runtime branching on type

## Files to modify

| File | Changes |
|------|---------|
| `include/injamm/detail/chunk.hpp` | None (chunk_at_var already has root) |
| `include/injamm/detail/ct_chunk.hpp` | Add `ct_at_var_kind::key` |
| `include/injamm/detail/parse.hpp` | Add `@root.` → placeholder, `@root` → at_var handling |
| `include/injamm/detail/ct_parse.hpp` | Same parser changes |
| `include/injamm/detail/bytecode.hpp` | Add `emit_at_root`, `emit_at_root_field`, `emit_at_root_field_raw`, `emit_at_key` opcodes |
| `include/injamm/detail/bytecode_compile.hpp` | Handle `@root`/`@root.field` in compiler |
| `include/injamm/detail/bytecode_exec.hpp` | Add handlers for new opcodes; add struct-field iteration in section |
| `include/injamm/detail/ct_render.hpp` | Add struct-field iteration in section; add `@key` in at_var |
| `include/injamm/detail/resolve.hpp` | Add `@key`, `@root`, `@root.field` resolution |
| `include/injamm/detail/loop_state.hpp` | Add `key` field |
| `include/injamm/injamm.hpp` | None (public API unchanged) |
| `include/injamm/escape_hatch.hpp` | None (public API unchanged) |
| `tests/test_injamm.cpp` | Add tests for `@root`, `@root.field`, `@key`, struct-field iteration |

## Backward Compatibility

- `@root` was previously a no-op in Bytecode VM — now it works. No existing correct behavior breaks.
- `@key` is new — no existing templates use it.
- Struct-field iteration extends section behavior for `glz_reflectable` types. Previously these were silently ignored in sections; now they iterate. This is technically a behavior change, but the previous behavior was a no-op, so no real output changes.
