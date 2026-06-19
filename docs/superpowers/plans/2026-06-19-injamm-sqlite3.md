# injamm-sqlite3 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a separate library `ext/injamm-sqlite3/` that renders templates directly from `sqlite3_stmt` without intermediate structs, reusing injamm's parser/compiler/bytecode format with a forked executor.

**Architecture:** Two new concepts (`runtime_field_accessible`, `forward_iterable`) in the separate library; executor forked from `bytecode_exec.hpp` (~2010 lines) with two new `if constexpr` branches; thin `runtime_engine<T>` wrapper; sqlite3 adapter types for zero-copy row access.

**Tech Stack:** C++23, injamm (upstream dependency, forked executor), sqlite3 (system `/usr/include/sqlite3.h`), Catch2 (tests).

---

### File Structure

```
ext/injamm-sqlite3/
├── CMakeLists.txt                              ← header-only INTERFACE lib
├── include/injamm/sqlite3/
│   ├── concept.hpp                             ← runtime_field_accessible, forward_iterable
│   ├── executor.hpp                            ← forked from bytecode_exec.hpp (2010→~2030 lines)
│   ├── engine.hpp                              ← runtime_engine<T> public API
│   └── adapter.hpp                             ← sqlite3_row_view, sqlite3_result
└── tests/
    ├── CMakeLists.txt
    ├── test_mock.cpp                           ← mock-type tests (no sqlite3 dep)
    └── test_sqlite3.cpp                        ← sqlite3 integration tests
```

---

### Task 1: Project Skeleton + Concept Headers

**Files:**
- Create: `ext/injamm-sqlite3/CMakeLists.txt`
- Create: `ext/injamm-sqlite3/include/injamm/sqlite3/concept.hpp`
- Create: `ext/injamm-sqlite3/tests/CMakeLists.txt`
- Create: `ext/injamm-sqlite3/tests/test_mock.cpp`
- Modify: `CMakeLists.txt` (root — add `add_subdirectory`)

- [ ] **Step 1: Create directory structure**

```bash
mkdir -p ext/injamm-sqlite3/include/injamm/sqlite3 ext/injamm-sqlite3/tests
```

- [ ] **Step 2: Write root CMakeLists.txt addition**

Add to the end of `CMakeLists.txt` (root):

```cmake
add_subdirectory(ext/injamm-sqlite3)
```

Add to `option` block if it exists, or just append after existing test targets:
```cmake
option(BUILD_INJAMM_SQLITE3 "Build injamm-sqlite3 extension" ON)
if(BUILD_INJAMM_SQLITE3)
  add_subdirectory(ext/injamm-sqlite3)
endif()
```

- [ ] **Step 3: Write `ext/injamm-sqlite3/CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.20)
project(injamm-sqlite3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(injamm-sqlite3 INTERFACE)
target_include_directories(injamm-sqlite3 INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(injamm-sqlite3 INTERFACE injamm)

if(BUILD_TEST)
  add_subdirectory(tests)
endif()
```

- [ ] **Step 4: Write `concept.hpp`**

```cpp
#pragma once

#include <string>
#include <string_view>
#include <type_traits>

namespace injamm::sqlite3 {

template <class T>
concept runtime_field_accessible = requires(T const& t, std::string_view key) {
  { t.find(key) } -> std::same_as<std::string>;
};

template <class T>
concept forward_iterable = requires(T& t) {
  typename T::value_type;
  { t.begin() };
  { t.end() };
};

} // namespace injamm::sqlite3
```

- [ ] **Step 5: Write mock-type test (`tests/test_mock.cpp`)**

```cpp
#include <injamm/sqlite3/concept.hpp>
#include <catch2/catch_test_macros.hpp>
#include <map>
#include <string>

using namespace injamm::sqlite3;

struct mock_row {
  std::string find(std::string_view key) const {
    auto it = data_.find(std::string(key));
    return it != data_.end() ? it->second : "";
  }
  std::map<std::string, std::string> data_;
};

struct mock_result {
  mock_row* rows_;
  std::size_t count_;

  struct iterator {
    mock_row* ptr_;
    mock_row* last_;
    auto& operator++() { ++ptr_; return *this; }
    auto& operator*() { return *ptr_; }
    bool operator!=(std::nullptr_t) const { return ptr_ < last_; }
  };

  using value_type = mock_row;
  iterator begin() { return iterator{rows_, rows_ + count_}; }
  std::nullptr_t end() const { return nullptr; }
};

TEST_CASE("concepts satisfy", "[concept]") {
  SECTION("mock_row is runtime_field_accessible") {
    static_assert(runtime_field_accessible<mock_row>);
  }
  SECTION("mock_result is forward_iterable") {
    static_assert(forward_iterable<mock_result>);
  }
  SECTION("mock_row find works") {
    mock_row r{{{"name", "Alice"}}};
    CHECK(r.find("name") == "Alice");
    CHECK(r.find("nonexistent") == "");
  }
}

// Verify existing concepts are NOT accidentally satisfied
#include <injamm/glz_dispatch.hpp>
using namespace injamm::detail;

TEST_CASE("existing concepts unaffected", "[concept]") {
  SECTION("mock_row is NOT glaze-reflectable") {
    static_assert(!ct_glz_reflectable<mock_row>);
  }
  SECTION("mock_row is NOT vector-like") {
    static_assert(!ct_is_vector_like<mock_row>);
  }
  SECTION("mock_row is NOT map-like") {
    static_assert(!ct_is_map_like<mock_row>);
  }
  SECTION("mock_result is NOT set-like (no size())") {
    static_assert(!ct_is_set_like<mock_result>);
  }
}
```

- [ ] **Step 6: Write `tests/CMakeLists.txt`**

```cmake
find_package(Catch2 REQUIRED)

add_executable(injamm-sqlite3_mock_test test_mock.cpp)
target_link_libraries(injamm-sqlite3_mock_test PRIVATE injamm-sqlite3 Catch2::Catch2)
target_compile_definitions(injamm-sqlite3_mock_test PRIVATE CATCH_CONFIG_MAIN)
catch_discover_tests(injamm-sqlite3_mock_test)
```

- [ ] **Step 7: Build and run test to verify it fails (nothing implemented yet)**

```bash
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=~/vm/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --target injamm-sqlite3_mock_test 2>&1 | head -30
# Expected: linking works, concepts are defined
./build/ext/injamm-sqlite3/tests/injamm-sqlite3_mock_test
# Expected: all tests pass (concepts exist, mock types satisfy)
```

---

### Task 2: Fork Executor

**Files:**
- Create: `ext/injamm-sqlite3/include/injamm/sqlite3/executor.hpp`

This task copies `include/injamm/bytecode_exec.hpp` and applies two targeted edits.

- [ ] **Step 1: Copy bytecode_exec.hpp as base**

```bash
cp include/injamm/bytecode_exec.hpp ext/injamm-sqlite3/include/injamm/sqlite3/executor.hpp
```

- [ ] **Step 2: Update includes at top of executor.hpp**

Replace:
```cpp
#include "../injamm.hpp"
#include "bytecode.hpp"
#include "escape.hpp"
#include "filters.hpp"
#include "glz_dispatch.hpp"
#include "serialize_value.hpp"
```
With:
```cpp
#include <injamm/bytecode.hpp>
#include <injamm/escape.hpp>
#include <injamm/filters.hpp>
#include <injamm/glz_dispatch.hpp>
#include <injamm/serialize_value.hpp>
#include <injamm/types.hpp>
#include <injamm/sqlite3/concept.hpp>
```

And remove the THREADED_DISPATCH block at the top (or keep it — GCC still works).

- [ ] **Step 3: Rename namespace**

Replace:
```cpp
namespace injamm::detail {
```
With:
```cpp
namespace injamm::sqlite3::detail {
```

- [ ] **Step 4: Add `runtime_field_accessible` branch to `for_each_field`**

Find the end of the `for_each_field` function where it returns `{}` for non-glaze types.

The function currently ends:
```cpp
  }
  return {};
}
```

Replace the entire end section (after the `ct_glz_reflectable` block's closing brace) with:

```cpp
  }
  if constexpr (runtime_field_accessible<V>) {
    auto val = v.find(key);
    if constexpr (std::same_as<decltype(visitor(val)), void>) {
      visitor(val);
      return {};
    } else {
      return visitor(val);
    }
  }
  return {};
}
```

- [ ] **Step 5: Add `forward_iterable` branch to `L_emit_section`**

Find the end of the section dispatch chain (after `ct_glz_reflectable<FT>` block and before the `return {};` that follows). Add before the closing `return {};`:

```cpp
    } else if constexpr (forward_iterable<FT>) {
      using elem_t = typename FT::value_type;
      bc_loop_state ls;
      ls.count = 0;  // unknown size
      for (auto& elem : field) {
        ls.continue_flag = false;
        bc_executor<elem_t, RootT> child_exec(bc_, elem, root_value_, &ls, out_);
        auto r2 = child_exec.execute_impl(pc + 1, body_end - 1);
        if (!r2)
          return r2;
        if (ls.continue_flag) {
          ls.continue_flag = false;
          continue;
        }
        if (ls.break_flag)
          break;
        ++ls.index;
      }
```

---

### Task 3: Engine Wrapper

**Files:**
- Create: `ext/injamm-sqlite3/include/injamm/sqlite3/engine.hpp`

- [ ] **Step 1: Write `engine.hpp`**

```cpp
#pragma once

#include <injamm/bytecode.hpp>
#include <injamm/bytecode_compile.hpp>
#include <injamm/sqlite3/concept.hpp>
#include <injamm/sqlite3/executor.hpp>
#include <expected>
#include <string>
#include <string_view>

namespace injamm::sqlite3 {

template <class T>
  requires runtime_field_accessible<T> || forward_iterable<T>
class runtime_engine {
  injamm::detail::bytecode bc_;
public:
  explicit runtime_engine(std::string_view tmpl, bool trim_blocks = false, bool lstrip_blocks = false)
    : bc_(injamm::detail::bc_compile<T>(tmpl, trim_blocks, lstrip_blocks)) {}

  [[nodiscard]] std::expected<std::string, injamm::detail::error_ctx> render(T const& value) const {
    return detail::bc_execute(bc_, value);
  }

  [[nodiscard]] std::expected<void, injamm::detail::error_ctx> render(T const& value, std::string& out) const {
    return detail::bc_execute_into(bc_, value, out);
  }
};

} // namespace injamm::sqlite3
```

- [ ] **Step 2: Add mock rendering test to `test_mock.cpp`**

At the bottom of test_mock.cpp, add:

```cpp
#include <injamm/sqlite3/engine.hpp>

TEST_CASE("mock rendering", "[engine]") {
  SECTION("single row, single var") {
    mock_row r{{{"name", "Alice"}, {"email", "alice@example.com"}}};
    auto eng = injamm::sqlite3::runtime_engine<mock_row>("Hello {{name}} ({{email}})");
    auto result = eng.render(r);
    REQUIRE(result.has_value());
    CHECK(*result == "Hello Alice (alice@example.com)");
  }

  SECTION("unknown key renders empty") {
    mock_row r{{{"name", "Bob"}}};
    auto eng = injamm::sqlite3::runtime_engine<mock_row>("Hi {{name}}, age={{age}}");
    auto result = eng.render(r);
    REQUIRE(result.has_value());
    CHECK(*result == "Hi Bob, age=");
  }
}
```

- [ ] **Step 3: Build and test**

```bash
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=~/vm/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --target injamm-sqlite3_mock_test 2>&1
./build/ext/injamm-sqlite3/tests/injamm-sqlite3_mock_test
# Expected: all tests pass (mock_row concept, mock_result concept, engine rendering)
```

---

### Task 4: Forward Iteration Engine Test

**Files:**
- Modify: `ext/injamm-sqlite3/tests/test_mock.cpp`

- [ ] **Step 1: Add forward iteration rendering test**

```cpp
TEST_CASE("forward iteration rendering", "[engine][iteration]") {
  SECTION("two rows with {{#.}}") {
    mock_row rows_arr[2] = {
      mock_row{{{"name", "Alice"}}},
      mock_row{{{"name", "Bob"}}}
    };
    mock_result res{rows_arr, 2};
    auto eng = injamm::sqlite3::runtime_engine<mock_result>(
      "<ul>{{#.}}<li>{{name}}</li>{{/.}}</ul>"
    );
    auto result = eng.render(res);
    REQUIRE(result.has_value());
    CHECK(*result == "<ul><li>Alice</li><li>Bob</li></ul>");
  }

  SECTION("loop.index works") {
    mock_row rows_arr[2] = {
      mock_row{{{"name", "A"}}},
      mock_row{{{"name", "B"}}}
    };
    mock_result res{rows_arr, 2};
    auto eng = injamm::sqlite3::runtime_engine<mock_result>(
      "{{#.}}{{loop.index}}:{{name}} {{/.}}"
    );
    auto result = eng.render(res);
    REQUIRE(result.has_value());
    CHECK(*result == "0:A 1:B ");
  }
}
```

- [ ] **Step 2: Build and test**

```bash
cmake --build build --target injamm-sqlite3_mock_test 2>&1
./build/ext/injamm-sqlite3/tests/injamm-sqlite3_mock_test
# Expected: all tests pass including iteration tests
```

---

### Task 5: SQLite3 Adapter

**Files:**
- Create: `ext/injamm-sqlite3/include/injamm/sqlite3/adapter.hpp`
- Create: `ext/injamm-sqlite3/tests/test_sqlite3.cpp`
- Modify: `ext/injamm-sqlite3/tests/CMakeLists.txt`

- [ ] **Step 1: Write `adapter.hpp`**

```cpp
#pragma once

#include <injamm/sqlite3/concept.hpp>
#include <sqlite3.h>
#include <charconv>
#include <string>
#include <string_view>

namespace injamm::sqlite3 {

struct sqlite3_row_view {
  sqlite3_stmt* stmt_;

  std::string find(std::string_view key) const {
    int n = sqlite3_column_count(stmt_);
    for (int i = 0; i < n; ++i) {
      if (key == sqlite3_column_name(stmt_, i)) {
        auto t = sqlite3_column_type(stmt_, i);
        switch (t) {
          case SQLITE_INTEGER: {
            auto val = sqlite3_column_int64(stmt_, i);
            char buf[24];
            auto [p, ec] = std::to_chars(buf, buf + sizeof(buf), val);
            return std::string(buf, static_cast<std::size_t>(p - buf));
          }
          case SQLITE_FLOAT: {
            auto val = sqlite3_column_double(stmt_, i);
            char buf[64];
            auto [p, ec] = std::to_chars(buf, buf + sizeof(buf), val);
            return std::string(buf, static_cast<std::size_t>(p - buf));
          }
          case SQLITE_TEXT: {
            auto text = sqlite3_column_text(stmt_, i);
            auto len = sqlite3_column_bytes(stmt_, i);
            return std::string(reinterpret_cast<const char*>(text), static_cast<std::size_t>(len));
          }
          default:
            return "";
        }
      }
    }
    return "";
  }
};

struct sqlite3_result {
  sqlite3_stmt* stmt_;
  bool started_ = false;

  using value_type = sqlite3_row_view;

  struct sentinel {};
  struct iterator {
    sqlite3_stmt* stmt_;
    int rc_ = SQLITE_ROW;
    iterator& operator++() { rc_ = sqlite3_step(stmt_); return *this; }
    sqlite3_row_view operator*() const { return sqlite3_row_view{stmt_}; }
    bool operator!=(sentinel) const { return rc_ == SQLITE_ROW; }
  };

  iterator begin() {
    if (!started_) {
      started_ = true;
      return iterator{stmt_, sqlite3_step(stmt_)};
    }
    return iterator{nullptr, SQLITE_DONE};
  }
  sentinel end() const { return {}; }
};

} // namespace injamm::sqlite3
```

- [ ] **Step 2: Write `tests/test_sqlite3.cpp`**

```cpp
#include <injamm/sqlite3/engine.hpp>
#include <injamm/sqlite3/adapter.hpp>
#include <catch2/catch_test_macros.hpp>
#include <sqlite3.h>
#include <string>

struct test_db {
  sqlite3* db = nullptr;
  test_db() {
    sqlite3_open(":memory:", &db);
    sqlite3_exec(db, "CREATE TABLE users (id INTEGER, name TEXT, email TEXT)", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "INSERT INTO users VALUES (1, 'Alice', 'alice@example.com')", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "INSERT INTO users VALUES (2, 'Bob', 'bob@example.com')", nullptr, nullptr, nullptr);
  }
  ~test_db() { sqlite3_close(db); }
};

sqlite3_stmt* prepare(test_db& db, const char* sql) {
  sqlite3_stmt* stmt;
  sqlite3_prepare_v2(db.db, sql, -1, &stmt, nullptr);
  return stmt;
}

TEST_CASE("sqlite3 adapter", "[sqlite3]") {
  SECTION("single row rendering") {
    test_db db;
    sqlite3_stmt* stmt = prepare(db, "SELECT name, email FROM users WHERE id = 1");
    REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);

    auto row = injamm::sqlite3::sqlite3_row_view{stmt};
    auto eng = injamm::sqlite3::runtime_engine<injamm::sqlite3::sqlite3_row_view>("{{name}} <{{email}}>");
    auto result = eng.render(row);
    REQUIRE(result.has_value());
    CHECK(*result == "Alice <alice@example.com>");

    sqlite3_finalize(stmt);
  }

  SECTION("multiple rows with {{#.}}") {
    test_db db;
    sqlite3_stmt* stmt = prepare(db, "SELECT name FROM users ORDER BY id");
    auto result_set = injamm::sqlite3::sqlite3_result{stmt};

    auto eng = injamm::sqlite3::runtime_engine<injamm::sqlite3::sqlite3_result>(
      "{{#.}}{{name}} {{/.}}"
    );
    auto result = eng.render(result_set);
    REQUIRE(result.has_value());
    CHECK(*result == "Alice Bob ");

    sqlite3_finalize(stmt);
  }

  SECTION("integer column") {
    test_db db;
    sqlite3_stmt* stmt = prepare(db, "SELECT id, name FROM users WHERE id = 2");
    REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);

    auto row = injamm::sqlite3::sqlite3_row_view{stmt};
    auto eng = injamm::sqlite3::runtime_engine<injamm::sqlite3::sqlite3_row_view>(
      "{{id}}:{{name}}"
    );
    auto result = eng.render(row);
    REQUIRE(result.has_value());
    CHECK(*result == "2:Bob");

    sqlite3_finalize(stmt);
  }
}
```

- [ ] **Step 3: Update `tests/CMakeLists.txt` for sqlite3 tests**

Add after the mock test target:

```cmake
find_package(SQLite3 REQUIRED)

add_executable(injamm-sqlite3_test test_sqlite3.cpp)
target_link_libraries(injamm-sqlite3_test PRIVATE injamm-sqlite3 Catch2::Catch2 SQLite::SQLite3)
target_compile_definitions(injamm-sqlite3_test PRIVATE CATCH_CONFIG_MAIN)
catch_discover_tests(injamm-sqlite3_test)
```

- [ ] **Step 4: Build and test**

```bash
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=~/vm/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --target injamm-sqlite3_test 2>&1
./build/ext/injamm-sqlite3/tests/injamm-sqlite3_test
# Expected: sqlite3 adapter tests all pass
# Run ALL tests:
ctest --test-dir build -R injamm-sqlite3 -V
```

---

### Task 6: Final Verification

- [ ] **Step 1: Build everything and run all tests**

```bash
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=~/vm/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build 2>&1
ctest --test-dir build -V
```

Expected:
- Existing injamm tests still pass (no regression)
- `injamm-sqlite3_mock_test` passes
- `injamm-sqlite3_test` passes (requires sqlite3)

- [ ] **Step 2: Commit**

```bash
git add ext/ CMakeLists.txt docs/superpowers/plans/2026-06-19-injamm-sqlite3.md docs/superpowers/specs/2026-06-19-resultset-template-design.md
git commit -m "feat: add injamm-sqlite3 extension — direct sqlite3_stmt template rendering"
```
