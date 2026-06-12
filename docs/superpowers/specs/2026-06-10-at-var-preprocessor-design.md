# `@var(xxx)` Preprocessor Constants — Design Spec

## Overview

Add C-preprocessor-style constant substitution to the injamm template engine.
`@var(xxx)` inside `{{...}}` tags is replaced with a constant value **before**
template parsing, enabling field-name aliasing, structural parameterization,
and compile-time constant inlining.

## Two APIs

### Runtime — `engine<T>`

```cpp
template <class T>
class engine {
  // Existing (unchanged)
  explicit engine(std::string_view tmpl);

  // New: accepts any map-like container
  template <class ConstMap>
  engine(std::string_view tmpl, ConstMap const& consts);
};
```

`ConstMap` requirements:
- `consts.find(key)` returns an iterator comparable to `consts.end()`
- iterator `->second` is convertible to `std::string_view`
- `key` is `std::string_view` (caller passes the var name)

### NTTP — `render<fixed_string, fixed_string...>`

```cpp
template <fixed_string Tmpl, fixed_string... Entries, class T>
[[nodiscard]] expected<std::string> render(T const& value);
```

`Entries...` are key-value pairs: `"key1", "val1", "key2", "val2", ...`
Must be an even number of entries (compile-time assertion).

## Preprocessing Algorithm

Text-level substitution before the template parser runs.

1. Scan the template string for `{{...}}` ranges (`{{{...}}}` raw tags handled)
2. Inside each `{{...}}`, find `@var(name)` patterns
3. Replace `@var(name)` with the constant value for `name`
4. If the expanded value contains `@var(yyy)`, recursively expand
5. Repeat until no more `@var(...)` found in any `{{...}}` range
6. Pass the fully expanded template to `bc_compile<T>()` (runtime) or `ct_parse_into()` (NTTP)

### Example

```
Template:  "Hello {{@var(field)}}! {{#@var(sec)}}{{item}}{{/@var(sec)}}"
Constants: {field → "username", sec → "items"}
Context:   {username: "Alice", items: [{item: "A"}, {item: "B"}]}

After expansion:
           "Hello {{username}}! {{#items}}{{item}}{{/items}}"

After rendering:
           "Hello Alice! AB"
```

## Edge Cases

| Case | Behavior |
|------|----------|
| `@var(a)` expands to `@var(b)`, `@var(b)` expands to `val` | Recursive expansion: final is `val` |
| Circular reference (`a→@var(b)`, `b→@var(a)`) | Max depth (100) reached → `error_code::syntax_error` |
| Undefined `@var(unknown)` | `error_code::unknown_key` |
| `@var(...)` outside `{{...}}` | Not processed; treated as literal text |
| Constant value containing `{{...}}` | **Not** interpreted as template syntax (user spec) |
| `{{@var(a) \| upper}}` | `@var(a)` expanded first, then `\| upper` applied to result |
| `{{{@var(a)}}}` | Raw tag, `@var(a)` expanded inside |

## Implementation Plan

### Files to modify

| File | Change |
|------|--------|
| `include/injamm/parse.hpp` | Add `expand_var_refs()` / `expand_vars_in_template()` free functions |
| `include/injamm/bytecode_compile.hpp` | Add `bc_compile<T>(tmpl, consts)` overload |
| `include/injamm/escape_hatch.hpp` | New `engine` constructor + `render` NTTP overload |
| `include/injamm/ct_parse.hpp` | No changes needed (receives already-expanded string) |
| `tests/test_injamm.cpp` | Runtime tests |
| `tests/test_injamm_ct.cpp` | NTTP CT tests |

### Error integration

The preprocessor returns `expected<std::string, error_ctx>` to propagate
unknown-key and circular-reference errors through the existing error pipeline.

## Non-Goals

- `@var(xxx)` outside `{{...}}` — explicitly excluded
- Runtime re-evaluation of `@var(...)` — compile/construction-time only
- Dynamic constant mutation — constants are fixed after construction
