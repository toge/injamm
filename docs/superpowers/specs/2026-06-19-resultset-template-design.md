# ResultSet → HTML Direct Rendering: injamm-sqlite3 Design

## Overview

Server-side HTML generation (HTMX, datastar) often follows: SQL query → ResultSet → struct → template → HTML.
A separate library `injamm-sqlite3` eliminates the intermediate struct by rendering directly from `sqlite3_stmt`,
reusing injamm's parser and bytecode format with a modified executor for runtime field access.

## Status: Design Document (Pre-Implementation)

## Problem

Web apps rendering HTML server-side:

```
SQL query → sqlite3_step → copy to struct → engine<T>.render(struct) → HTML
                           ╰──────────╯
                          "データの組替え"
```

This copy is not the bottleneck (dominant costs are DB I/O and string processing), but eliminating it
simplifies the programming model: no struct definition needed when the template is the schema.

## Solution: `injamm-sqlite3` (Separate Library)

- **Separate repository**, depends on injamm for parsing + bytecode
- Does **NOT** modify injamm core
- Copies injamm's executor and adds `runtime_field_accessible` dispatch

## Architecture

```
injamm (upstream, untouched)
  ├── bytecode.hpp            → copied/imported
  ├── bytecode_compile.hpp    → copied/imported
  ├── parse.hpp               → copied/imported
  ├── escape.hpp              → copied/imported
  └── serialize_value.hpp     → copied/imported

injamm-sqlite3 (separate repo)
  ├── executor.hpp            → forked from bytecode_exec.hpp + runtime_field_accessible branch
  ├── runtime_engine.hpp      → public API (compiler + custom executor)
  ├── sqlite3_adapter.hpp     → sqlite3_row_view, sqlite3_result
  └── concept.hpp             → runtime_field_accessible<T>, forward_iterable<T>
```

### Changes from injamm's executor

Two new branches in the forked executor:

**1. Variable access** (`for_each_field`):

```cpp
template <class V, class F>
auto for_each_field(V const& v, std::string_view key, ..., F&& visitor) const {
  if constexpr (ct_glz_reflectable<V>) { /* existing: O(1) + linear fallback */ }
  else if constexpr (runtime_field_accessible<V>) { /* NEW */
    auto val = v.find(key);
    visitor(val);  // val is std::string
  }
  else { /* existing: silent return */ }
}
```

**2. Section iteration** (`L_emit_section`):

```cpp
if constexpr (ct_is_vector_like<FT>) { /* existing */ }
else if constexpr (/* ... existing branches ... */) {}
else if constexpr (forward_iterable<FT>) { /* NEW */
  // forward-only range-for, no size(), no random access
  for (auto& elem : field) {
    child_executor child(bc_, elem, ...);
    if (break_flag) break;
  }
}
```

## Types

### `runtime_field_accessible<T>` concept

```cpp
template <class T>
concept runtime_field_accessible = requires(T const& t, std::string_view key) {
  { t.find(key) } -> std::same_as<std::string>;
};
```

Values are returned as `std::string`. INTEGER/REAL columns are converted to string
inside `find()` via `std::to_string`/`std::to_chars`. This matches the cost of the
struct-based path (serialization to string always happens at render time).

### `forward_iterable<T>` concept

```cpp
template <class T>
concept forward_iterable = requires(T& t) {
  typename T::value_type;
  { t.begin() };
  { t.end() };
};
```

No `size()` requirement — forward-only cursor. `loop.size` and `loop.is_last`
are unavailable. `loop.index` and `loop.is_first` work.

Dispatch order in the section handler (added after `ct_is_set_like`):

```cpp
// in executor.hpp, L_emit_section branch:
if constexpr (ct_is_vector_like<FT>) { /* existing */ }
else if constexpr (std::same_as<FT, bool>) { /* existing */ }
else if constexpr (ct_is_map_like<FT>) { /* existing */ }
else if constexpr (ct_is_set_like<FT>) { /* existing */ }
else if constexpr (ct_glz_reflectable<FT>) { /* existing */ }
else if constexpr (forward_iterable<FT>) { /* NEW */
  for (auto& elem : field) {
    child_executor child(bc_, elem, ...);
    if (break_flag) break;
  }
}
```

This goes last so existing types matching earlier concepts are unaffected.

### `sqlite3_row_view`

```cpp
struct sqlite3_row_view {
  sqlite3_stmt* stmt_;

  std::string find(std::string_view key) const {
    int n = sqlite3_column_count(stmt_);
    for (int i = 0; i < n; i++) {
      if (key == sqlite3_column_name(stmt_, i)) {
        auto t = sqlite3_column_type(stmt_, i);
        switch (t) {
          case SQLITE_INTEGER: return std::to_string(sqlite3_column_int64(stmt_, i));
          case SQLITE_FLOAT:   // to_chars → string
          case SQLITE_TEXT:    return /* string_view → string copy */;
          default:             return "";
        }
      }
    }
    return "";
  }
};
```

### `sqlite3_result` (forward-only cursor)

```cpp
struct sqlite3_result {
  using value_type = sqlite3_row_view;

  struct sentinel {};
  struct iterator {
    sqlite3_stmt* stmt_;
    int rc_;
    auto& operator++() { rc_ = sqlite3_step(stmt_); return *this; }
    auto operator*() const { return sqlite3_row_view{stmt_}; }
    bool operator!=(sentinel) const { return rc_ == SQLITE_ROW; }
  };

  sqlite3_stmt* stmt_;
  iterator begin() { return {stmt_, sqlite3_step(stmt_)}; }
  sentinel end() const { return {}; }
};
```

## API Surface

```cpp
template <runtime_field_accessible T>
class runtime_engine {
  detail::bytecode bc_;
public:
  runtime_engine(std::string_view tmpl);
  expected<std::string> render(T const& value) const;
};

// Usage:
sqlite3_stmt* stmt;
/* prepare + step */
sqlite3_result rows{stmt};
runtime_engine<sqlite3_result>("<ul>{{#.}}<li>{{name}} ({{email}})</li>{{/.}}</ul>")
    .render(rows);
```

## Supported Template Features

| Feature | Status | Note |
|---------|--------|------|
| `{{var}}` / `{{{var}}}` | ✅ | Escaped / raw |
| String filters (`upper`, `trim`, etc.) | ✅ | Value is string, filters work |
| `{{#.}}...{{/.}}` (section iteration) | ✅ | Forward-only cursor |
| `{{^var}}...{{/.}}` (inverted section) | ✅ | Non-empty check on string value |
| `{{loop.index}}` / `{{loop.index1}}` | ✅ | Counter maintained by executor |
| `{{loop.is_first}}` | ✅ | `index == 0` |
| `{{loop.is_last}}` | ❌ | Forward cursor can't know |
| `{{loop.size}}` | ❌ | Not known without COUNT query |
| `{{loop.key}}` | ❌ | Not applicable to positional rows |
| `{{#if age > 18}}` | ❌ | Values are strings, not arithmetic |
| Int/float filters | ⚠️ | String→int conversion not automatic |
| `{{this}}` | ✅ | Serializes current row(s) |
| Nested dot paths `{{addr.city}}` | ❌ | Runtime type can't reflect nested |

## Limitations

1. **Type erasure**: All values are strings. Integer comparisons and numeric
   filters (`zerofill`, `hex`, etc.) require explicit conversion or a future
   variant-based approach.
2. **Forward-only**: One-pass iteration. No `loop.is_last`, `loop.size`.
3. **Single cursor lifetime**: `sqlite3_result` cannot be iterated twice
   without re-preparing the statement.
4. **Forked executor**: Tracks injamm's bytecode format. Format changes in
   injamm require syncing.

## Performance Characteristics

| Aspect | vs. struct-based | Note |
|--------|-----------------|------|
| INTEGER column | ≈ same | `to_string` in find() ≈ `to_chars` in serialize |
| TEXT column | ≈ same | One extra memcpy vs. struct path |
| Memory allocation | ≈ same | One string per field in both paths |
| No struct definition | ✅ | Programming model win, not perf win |
| Zero-copy TEXT | ❌ | Not achieved (need lifetime management) |

The performance argument for this approach is primarily operational
simplicity (no struct to define/update when the template changes),
not raw throughput.

## Future Extensions

- **Variant return type**: `std::variant<int64_t, double, std::string>` for
  type-preserving filter dispatch. Requires executor changes.
- **SOCI adapter**: `soci_row_view` analogous to `sqlite3_row_view`.
- **Pre-counting**: `SELECT COUNT(*)` wrapper to enable `loop.size`/`loop.is_last`.
- **Copy-on-first-access caching**: Materialize row lazily for re-iteration.

## Relationship to injamm

This design keeps injamm focused on its core philosophy:

| Principle | Protected? |
|-----------|-----------|
| Compile-time field resolution | ✅ Unchanged |
| Type-safe struct access | ✅ Unchanged |
| Zero-vtable dispatch | ✅ Unchanged |
| No runtime type erasure in core | ✅ Moved to separate library |
