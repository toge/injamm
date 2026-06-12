# Glaze Error Handling Implementation Plan

> **For Gemini:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Improve error handling for `glz::write_json` by replacing `(void)` casts with proper checks and returning `std::unexpected` with `error_code::syntax_error`.

**Architecture:** Update `ct_render.hpp` and `bytecode_exec.hpp` to check the return value of `glz::write_json`. On failure, return an `error_ctx` with `error_code::syntax_error`.

**Tech Stack:** C++, glaze, injamm.

---

### Task 1: Update `include/injamm/ct_render.hpp`

**Files:**
- Modify: `include/injamm/ct_render.hpp`

**Step 1: Write a test case that exercises `{{this}}` with a glaze reflectable type**

Since it's hard to make `glz::write_json` fail, we'll first ensure it works as expected, then apply the change.

```cpp
// In tests/test_injamm_ct.cpp
struct reflectable_thing {
  int a = 42;
};
GLZ_META(reflectable_thing, a);

TEST_CASE("Compile-time: {{this}} with reflectable thing") {
  reflectable_thing val;
  auto result = injamm::render_ct("{{this}}", val);
  CHECK(result.value() == "{\"a\":42}");
}
```

**Step 2: Run tests to verify they pass**

Run: `./build.sh && ./build/injamm_tests`
Expected: PASS

**Step 3: Modify `include/injamm/ct_render.hpp` to handle `glz::write_json` error**

Replace:
```cpp
    } else if constexpr (ct_glz_reflectable<T>) {
      std::string tmp;
      (void)glz::write_json(value, tmp);
      if constexpr (std::is_same_v<Mode, mustache_tag>) {
        if (!raw) {
          html_escape_into(out, tmp);
          return {};
        }
      }
      out.append(tmp);
    }
```
With:
```cpp
    } else if constexpr (ct_glz_reflectable<T>) {
      std::string tmp;
      if (auto ec = glz::write_json(value, tmp)) {
        return std::unexpected(error_ctx{.ec = error_code::syntax_error});
      }
      if constexpr (std::is_same_v<Mode, mustache_tag>) {
        if (!raw) {
          html_escape_into(out, tmp);
          return {};
        }
      }
      out.append(tmp);
    }
```

**Step 4: Run tests to verify they still pass**

Run: `./build.sh && ./build/injamm_tests`
Expected: PASS

**Step 5: Commit changes**

```bash
git add include/injamm/ct_render.hpp
git commit -m "refactor: handle glz::write_json errors in ct_render.hpp"
```

---

### Task 2: Update `include/injamm/bytecode_exec.hpp`

**Files:**
- Modify: `include/injamm/bytecode_exec.hpp`

**Step 1: Write a test case that exercises `{{this}}` with a glaze reflectable type in runtime execution**

```cpp
// In tests/test_injamm.cpp
struct reflectable_thing {
  int a = 42;
};
GLZ_META(reflectable_thing, a);

TEST_CASE("Runtime: {{this}} with reflectable thing") {
  reflectable_thing val;
  auto result = injamm::render("{{this}}", val);
  CHECK(result.value() == "{\"a\":42}");
}
```

**Step 2: Run tests to verify they pass**

Run: `./build.sh && ./build/injamm_tests`
Expected: PASS

**Step 3: Update `L_emit_this` in `include/injamm/bytecode_exec.hpp`**

Around line 781:
```cpp
    L_emit_this: {
      if constexpr (serializable_v<T>) {
        serialize_value(out_, value_);
      } else if constexpr (ct_glz_reflectable<T>) {
        if (auto ec = glz::write_json(value_, out_)) {
          return std::unexpected(error_ctx{.position = pc, .ec = error_code::syntax_error});
        }
      }
      ++pc;
      DISPATCH();
    }
```

**Step 4: Update `bc_opcode::emit_this` in `include/injamm/bytecode_exec.hpp`**

Around line 1383:
```cpp
        case bc_opcode::emit_this: {
          if constexpr (serializable_v<T>) {
            serialize_value(out_, value_);
          } else if constexpr (ct_glz_reflectable<T>) {
            if (auto ec = glz::write_json(value_, out_)) {
              return std::unexpected(error_ctx{.position = pc, .ec = error_code::syntax_error});
            }
          }
          ++pc;
          break;
        }
```

**Step 5: Run tests to verify they still pass**

Run: `./build.sh && ./build/injamm_tests`
Expected: PASS

**Step 6: Commit changes**

```bash
git add include/injamm/bytecode_exec.hpp
git commit -m "refactor: handle glz::write_json errors in bytecode_exec.hpp"
```
