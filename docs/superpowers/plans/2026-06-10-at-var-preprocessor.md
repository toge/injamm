# `@var(xxx)` Preprocessor Constants — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add C-preprocessor-style constant substitution `@var(xxx)` inside `{{...}}` tags, for both runtime (`engine<T>`) and NTTP (`render<fixed_string>`) APIs.

**Architecture:** Text-level preprocessing before template parsing — scan template for `{{...}}` ranges, replace `@var(name)` with constant values inside those ranges, then feed result to existing parser/compiler. No changes to parser, VM, or render internals.

**Tech Stack:** C++26, glaze, Catch2

---

### Task 1: Preprocessing utilities in `parse.hpp`

**Files:**
- Modify: `include/injamm/parse.hpp` (append at end, before `#endif`)

- [ ] **Step 1: Add `expand_var_refs()` — expands @var(name) within a single inner-content string**

```cpp
namespace injamm::detail {

// Expand @var(name) references inside a {{...}} inner-content string.
// Returns the expanded string or error_ctx (undefined constant / circular ref).
// Recursively expands @var(yyy) found inside constant values.
template <class ConstMap>
expected<std::string, error_ctx> expand_var_refs(std::string_view content, ConstMap const& consts, int depth = 0) {
  if (depth > 100) {
    return std::unexpected(error_ctx{0, error_code::syntax_error, "circular @var reference"});
  }
  std::string result;
  std::size_t last = 0;
  while (true) {
    auto var_start = content.find("@var(", last);
    if (var_start == std::string_view::npos) break;
    auto paren_close = content.find(")", var_start + 5);
    if (paren_close == std::string_view::npos) break;
    result.append(content, last, var_start - last);
    auto name = content.substr(var_start + 5, paren_close - var_start - 5);
    auto it = consts.find(name);
    if (it == consts.end()) {
      return std::unexpected(error_ctx{var_start, error_code::unknown_key, "undefined @var constant"});
    }
    std::string_view raw_val{it->second};
    // Recursively expand @var in the constant value
    auto expanded_val = expand_var_refs(raw_val, consts, depth + 1);
    if (!expanded_val) {
      return std::unexpected(expanded_val.error());
    }
    result += *expanded_val;
    last = paren_close + 1;
  }
  result.append(content, last, content.size() - last);
  return result;
}

// Scan template for {{...}} ranges and expand @var(name) inside them.
// Handles {{{...}}} raw tags. Loops until no more @var found.
template <class ConstMap>
expected<std::string, error_ctx> expand_vars_in_template(std::string_view tmpl, ConstMap const& consts) {
  std::string result(tmpl);
  bool any_changed = true;
  for (int iter = 0; iter < 100 && any_changed; ++iter) {
    any_changed = false;
    std::string next;
    std::size_t last = 0;
    while (true) {
      auto open = result.find("{{", last);
      if (open == std::string_view::npos) break;
      // Handle {{{ raw tag
      if (open + 2 < result.size() && result[open + 2] == '{') {
        auto raw_close = result.find("}}}", open + 3);
        if (raw_close == std::string_view::npos) {
          next.append(result, last, result.size() - last);
          last = result.size();
          break;
        }
        next.append(result, last, raw_close + 3 - last);
        last = raw_close + 3;
        continue;
      }
      auto close = result.find("}}", open + 2);
      if (close == std::string_view::npos) {
        next.append(result, last, result.size() - last);
        last = result.size();
        break;
      }
      next.append(result, last, open - last);
      auto inner = std::string_view{result}.substr(open + 2, close - open - 2);
      auto expanded = expand_var_refs(inner, consts);
      if (!expanded) {
        return std::unexpected(expanded.error());
      }
      if (*expanded != inner) any_changed = true;
      next += "{{";
      next += *expanded;
      next += "}}";
      last = close + 2;
    }
    if (last < result.size()) {
      next.append(result, last, result.size() - last);
    }
    result = std::move(next);
  }
  return result;
}

} // namespace injamm::detail
```

- [ ] **Step 2: Build check — verify `parse.hpp` compiles without errors**

Run: `cmake --build build 2>&1 | head -30`
Expected: compilation succeeds (may fail later at link, but parse.hpp itself is fine)

---

### Task 2: `bc_compile` overload in `bytecode_compile.hpp`

**Files:**
- Modify: `include/injamm/bytecode_compile.hpp` (add new overload after `bc_compile<T>`)

- [ ] **Step 1: Add `bc_compile<T>(tmpl, consts)` overload**

```cpp
template <class T, class ConstMap>
bytecode bc_compile(std::string_view tmpl, ConstMap const& consts) {
  auto expanded = expand_vars_in_template(tmpl, consts);
  if (!expanded) {
    bytecode err_bc;
    err_bc.error = expanded.error();
    return err_bc;
  }
  return bc_compile<T>(*expanded);
}
```

- [ ] **Step 2: Update `bytecode` struct in `bytecode.hpp` to carry an optional error**

Check `bytecode.hpp` to see if it already has an error field. If not, add one:
```cpp
struct bytecode {
  std::vector<bc_instruction> instructions;
  std::vector<std::string> literals;
  std::vector<bc_var_ref> var_refs;
  error_ctx error{}; // non-zero ec means compile-time error
  
  // ... existing methods ...
};
```

Run: `cmake --build build 2>&1 | head -30`
Expected: compilation succeeds

---

### Task 3: Runtime `engine` constructor in `escape_hatch.hpp`

**Files:**
- Modify: `include/injamm/escape_hatch.hpp` (add new constructor to `engine<T>`)

- [ ] **Step 1: Add map-like constructor to `engine<T>`**

```cpp
template <class ConstMap>
engine(std::string_view tmpl, ConstMap const& consts) : bc_(detail::bc_compile<T>(tmpl, consts)) {}
```

And update `engine::render()` to check `bc_.error`:
```cpp
[[nodiscard]] expected<std::string> render(T const& value) const {
  if (bc_.error.ec != error_code::none) {
    return std::unexpected(bc_.error);
  }
  return detail::bc_execute(bc_, value);
}
```

- [ ] **Step 2: Build check**

Run: `cmake --build build 2>&1 | head -30`
Expected: compilation succeeds

---

### Task 4: NTTP `render<fixed_string, fixed_string...>` overload in `escape_hatch.hpp`

**Files:**
- Modify: `include/injamm/escape_hatch.hpp`

- [ ] **Step 1: Add constexpr var table helper**

```cpp
namespace detail {

// Compile-time key-value lookup table from NTTP fixed_string pairs.
template <fixed_string... Entries>
struct ct_var_table {
  static constexpr std::size_t num = sizeof...(Entries);
  static_assert(num % 2 == 0, "@var entries must be key-value pairs (even count)");

  static constexpr std::array<std::string_view, num> entries{std::string_view{Entries.data, Entries.size()}...};

  static constexpr std::string_view lookup(std::string_view key) noexcept {
    for (std::size_t i = 0; i < num; i += 2) {
      if (entries[i] == key) return entries[i + 1];
    }
    return {}; // not found
  }
};

// Compile-time @var expansion: takes a template string and entries,
// returns the expanded template as a pair of char buffer and length.
template <fixed_string Tmpl, fixed_string... Entries>
struct ct_expanded_template {
  using table = ct_var_table<Entries...>;

  // Compute expanded size
  static constexpr std::size_t compute_size() {
    auto sv = std::string_view{Tmpl.data, Tmpl.size()};
    std::size_t sz = 0;
    std::size_t pos = 0;
    while (pos < sv.size()) {
      auto var_start = sv.find("@var(", pos);
      if (var_start == std::string_view::npos) { sz += sv.size() - pos; break; }
      sz += var_start - pos;
      auto close = sv.find(")", var_start + 5);
      if (close == std::string_view::npos) { sz += sv.size() - var_start; break; }
      auto name = sv.substr(var_start + 5, close - var_start - 5);
      auto val = table::lookup(name);
      sz += val.empty() ? (close - var_start + 1) : val.size();
      pos = close + 1;
    }
    return sz;
  }

  static constexpr std::size_t expanded_size = compute_size();

  static constexpr std::array<char, expanded_size + 1> data = []() {
    std::array<char, expanded_size + 1> arr{};
    auto sv = std::string_view{Tmpl.data, Tmpl.size()};
    std::size_t out = 0;
    std::size_t pos = 0;
    while (pos < sv.size()) {
      auto var_start = sv.find("@var(", pos);
      if (var_start == std::string_view::npos) {
        while (pos < sv.size()) arr[out++] = sv[pos++];
        break;
      }
      while (pos < var_start) arr[out++] = sv[pos++];
      auto close = sv.find(")", var_start + 5);
      if (close == std::string_view::npos) {
        while (pos < sv.size()) arr[out++] = sv[pos++];
        break;
      }
      auto name = sv.substr(var_start + 5, close - var_start - 5);
      auto val = table::lookup(name);
      if (!val.empty()) {
        for (auto c : val) arr[out++] = c;
      } else {
        for (auto i = var_start; i <= close; ++i) arr[out++] = sv[i];
      }
      pos = close + 1;
    }
    return arr;
  }();
};

} // namespace detail
```

- [ ] **Step 2: Add NTTP render overload**

```cpp
template <fixed_string Tmpl, fixed_string... Entries, class T>
[[nodiscard]] inline expected<std::string> render(T const& value) {
  using ET = detail::ct_expanded_template<Tmpl, Entries...>;
  constexpr std::string_view expanded_sv{ET::data.data(), ET::expanded_size};

  constexpr auto parsed = [&]() {
    detail::ct_parse_context<ET::expanded_size + 1> ctx;
    detail::ct_parse_into(ctx, expanded_sv);
    return detail::resolve_field_indices<T>(ctx.tmpl);
  }();

  std::string out;
  out.reserve(ET::expanded_size * 2);
  auto r = detail::ct_render_chunks<mustache_tag>(out, parsed, 0, parsed.size, value, value, nullptr);
  if (!r) {
    return std::unexpected(r.error());
  }
  return out;
}
```

- [ ] **Step 3: Build check**

Run: `cmake --build build 2>&1 | head -30`
Expected: compilation succeeds

---

### Task 5: Runtime tests

**Files:**
- Modify: `tests/test_injamm.cpp`

- [ ] **Step 1: Add test struct with glaze meta**

```cpp
struct AtVarUser {
  std::string name;
  int age;
};
template <>
struct glz::meta<AtVarUser> {
  static constexpr auto value = glz::object("name", &AtVarUser::name, "age", &AtVarUser::age);
};
```

- [ ] **Step 2: Write basic @var expansion test**

```cpp
TEST_CASE("@var basic expansion in engine", "[injamm][atvar]") {
  AtVarUser ctx{"Alice", 30};
  std::map<std::string, std::string, std::less<>> consts{{"f", "name"}};
  auto result = engine<AtVarUser>("Hello {{@var(f)}}!", consts).render(ctx);
  REQUIRE(result.has_value());
  CHECK(*result == "Hello Alice!");
}
```

- [ ] **Step 3: Write @var in section test**

```cpp
TEST_CASE("@var in section key", "[injamm][atvar]") {
  auto ctx = std::map<std::string, std::string>{{"title", "X"}, {"content", "Y"}};
  std::map<std::string, std::string, std::less<>> consts{{"sec", "title"}};
  auto result = engine<decltype(ctx)>("{{#@var(sec)}}{{.}}{{/@var(sec)}}", consts).render(ctx);
  REQUIRE(result.has_value());
  CHECK(*result == "X");
}
```

Wait, sections use `{{.}}` for this? Let me check... actually sections on maps iterate over key-value pairs. Let me adjust.

Actually, for a `std::map<std::string, std::string>`, iteration gives pairs. For `{{this}}` it serializes the pair. Let me use a simpler test:

```cpp
TEST_CASE("@var in section key with struct array", "[injamm][atvar]") {
  struct Item { std::string val; };
  template <> struct glz::meta<Item> {
    static constexpr auto value = glz::object("val", &Item::val);
  };
  struct ItemsCtx { std::vector<Item> items; };
  template <> struct glz::meta<ItemsCtx> {
    static constexpr auto value = glz::object("items", &ItemsCtx::items);
  };
  ItemsCtx ctx{{{"A"}, {"B"}}};
  std::map<std::string, std::string, std::less<>> consts{{"s", "items"}};
  auto result = engine<ItemsCtx>("{{#@var(s)}}{{val}}{{/@var(s)}}", consts).render(ctx);
  REQUIRE(result.has_value());
  CHECK(*result == "AB");
}
```

- [ ] **Step 4: Write @var with filter test**

```cpp
TEST_CASE("@var with filter", "[injamm][atvar]") {
  AtVarUser ctx{"alice", 30};
  std::map<std::string, std::string, std::less<>> consts{{"f", "name"}};
  auto result = engine<AtVarUser>("{{@var(f) | upper}}", consts).render(ctx);
  REQUIRE(result.has_value());
  CHECK(*result == "ALICE");
}
```

- [ ] **Step 5: Write undefined @var error test**

```cpp
TEST_CASE("@var undefined constant error", "[injamm][atvar]") {
  AtVarUser ctx{"Alice", 30};
  std::map<std::string, std::string, std::less<>> consts{};
  auto result = engine<AtVarUser>("{{@var(unknown)}}", consts).render(ctx);
  REQUIRE(!result.has_value());
  CHECK(result.error().ec == error_code::unknown_key);
}
```

- [ ] **Step 6: Build and run the new tests**

Run: `cmake --build build && ./build/injamm_tests "[atvar]"`
Expected: All 4 tests pass

---

### Task 6: NTTP compile-time tests

**Files:**
- Modify: `tests/test_injamm_ct.cpp`

- [ ] **Step 1: Write basic @var NTTP expansion test**

```cpp
TEST_CASE("@var basic expansion in render (NTTP)", "[injamm][ct][atvar]") {
  AtVarUser ctx{"Alice", 30};
  auto result = render<"Hello {{@var(f)}}!", "f", "name">(ctx);
  REQUIRE(result.has_value());
  CHECK(*result == "Hello Alice!");
}
```

(Reuse `AtVarUser` struct and its `glz::meta` — may need to define in a shared header or duplicate in both test files.)

- [ ] **Step 2: Write @var NTTP with raw tag**

```cpp
TEST_CASE("@var with raw tag (NTTP)", "[injamm][ct][atvar]") {
  AtVarUser ctx{"Alice", 30};
  auto result = render<"{{{@var(f)}}}", "f", "name">(ctx);
  REQUIRE(result.has_value());
  CHECK(*result == "Alice");
}
```

- [ ] **Step 3: Write @var NTTP in section**

```cpp
TEST_CASE("@var in section with NTTP", "[injamm][ct][atvar]") {
  struct Item { std::string val; };
  template <> struct glz::meta<Item> {
    static constexpr auto value = glz::object("val", &Item::val);
  };
  struct ItemsCtx { std::vector<Item> items; };
  template <> struct glz::meta<ItemsCtx> {
    static constexpr auto value = glz::object("items", &ItemsCtx::items);
  };
  ItemsCtx ctx{{{"A"}, {"B"}}};
  auto result = render<"{{#@var(s)}}{{val}}{{/@var(s)}}", "s", "items">(ctx);
  REQUIRE(result.has_value());
  CHECK(*result == "AB");
}
```

- [ ] **Step 4: Build and run the new tests**

Run: `cmake --build build && ./build/injamm_tests "[atvar]" && ./build/injamm_tests "[ct][atvar]"`
Expected: All tests pass

---

### Task 7: Full verification

- [ ] **Step 1: Run full test suite**

Run: `ctest --test-dir build -V`
Expected: All tests pass with no failures

- [ ] **Step 2: Final build check with warnings**

Run: `cmake --build build 2>&1 | grep -E "warning|error" || echo "Clean build"`
Expected: No warnings or errors
