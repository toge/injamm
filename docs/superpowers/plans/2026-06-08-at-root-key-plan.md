# @root and @key Implementation Plan

> **For agentic workers:** Execute tasks sequentially with benchmark verification after each step.

**Goal:** Add `@root` (root context access) and `@key` (struct-field iteration key) to injamm's Bytecode VM and NTTP paths, plus struct-field (map-like) section iteration.

**Architecture:** Modifies parser (`parse.hpp`, `ct_parse.hpp`), bytecode layer (`bytecode.hpp`, `bytecode_compile.hpp`, `bytecode_exec.hpp`), NTTP renderer (`ct_render.hpp`), resolver (`resolve.hpp`), and loop state. New opcodes for root access. Struct iteration adds a `glz_reflectable` branch to section handlers.

**Tech Stack:** C++26, Glaze reflection, computed-goto dispatch (GCC), `if constexpr` polymorphism.

**Benchmark verification:** After each implementation step, copy updated headers to `~/src/template-benchmark/build/vcpkg_installed/x64-linux/include/injamm/`, rebuild benchmarks with `cmake --build ~/src/template-benchmark/build --verbose --parallel 4`, run `bench_at_vars` and `bench_paths`.

---

### Task 1: @root Bytecode VM

**Files:**
- Modify: `include/injamm/detail/bytecode.hpp`
- Modify: `include/injamm/detail/bytecode_compile.hpp`
- Modify: `include/injamm/detail/bytecode_exec.hpp`
- Modify: `include/injamm/detail/ct_render.hpp`

- [ ] **Add `emit_at_root` opcode** to `bc_opcode` enum in `bytecode.hpp`
- [ ] **Handle root in compiler** — `bc_compiler::emit_at_var()` switch add case for `kind::root` emitting `emit_at_root`
- [ ] **Handle root in executor** — add `L_emit_at_root` handler and switch-case in `bc_executor::execute_impl` (both computed-goto and switch paths)
- [ ] **Copy headers, build, benchmark**:
  ```bash
  cp include/injamm/detail/bytecode.hpp include/injamm/detail/bytecode_compile.hpp include/injamm/detail/bytecode_exec.hpp include/injamm/detail/ct_render.hpp ~/src/template-benchmark/build/vcpkg_installed/x64-linux/include/injamm/detail/
  cp include/injamm/injamm.hpp ~/src/template-benchmark/build/vcpkg_installed/x64-linux/include/injamm/
  cmake --build ~/src/template-benchmark/build --verbose --parallel 4
  ~/src/template-benchmark/build/bench_at_vars --benchmark_time_unit=ns --benchmark_repetitions=3
  ~/src/template-benchmark/build/bench_paths --benchmark_time_unit=ns --benchmark_repetitions=3
  ```

### Task 2: @root.field parser + compiler + VM

**Files:**
- Modify: `include/injamm/detail/parse.hpp`
- Modify: `include/injamm/detail/ct_parse.hpp`
- Modify: `include/injamm/detail/bytecode.hpp`
- Modify: `include/injamm/detail/bytecode_compile.hpp`
- Modify: `include/injamm/detail/bytecode_exec.hpp`
- Modify: `include/injamm/detail/resolve.hpp`

Parse `@root.field` as `chunk_placeholder{"@root.field"}` (NTTP path handles it), and add `emit_at_root_field` / `emit_at_root_field_raw` opcodes for Bytecode VM.

- [ ] **Parser change** — In `parse.hpp`, add `@root.` prefix check before generic `@` handler. `@root.` → `chunk_placeholder{key}`. `@root` → `chunk_at_var{root}`.
- [ ] **CT parser change** — Same in `ct_parse.hpp`.
- [ ] **Add opcodes** — `emit_at_root_field` (opcode 19), `emit_at_root_field_raw` (opcode 20) in `bytecode.hpp`
- [ ] **Compiler** — In `bc_compiler::compile_body()`, detect `@root.` prefix: strip it, add var_ref with remaining path, emit `emit_at_root_field` (or `_raw`).
- [ ] **Executor handlers** — Add `L_emit_at_root_field`/`L_emit_at_root_field_raw`: resolve var_ref against `root_value_` via `for_each_field`. Both dispatch paths.
- [ ] **resolve.hpp** — Add `@root` and `@root.` handling to `resolve_value`.
- [ ] **Copy, build, benchmark**

### Task 3: @key enum + opcode + loop state

**Files:**
- Modify: `include/injamm/detail/chunk.hpp`
- Modify: `include/injamm/detail/ct_chunk.hpp`
- Modify: `include/injamm/detail/parse.hpp` (parse_at_kind)
- Modify: `include/injamm/detail/bytecode.hpp`
- Modify: `include/injamm/detail/bytecode_compile.hpp`
- Modify: `include/injamm/detail/bytecode_exec.hpp`
- Modify: `include/injamm/detail/ct_render.hpp`
- Modify: `include/injamm/detail/ct_parse.hpp`
- Modify: `include/injamm/detail/resolve.hpp`
- Modify: `include/injamm/detail/loop_state.hpp`

- [ ] Add `key` to `chunk_at_var::kind` and `ct_at_var_kind`
- [ ] Update `parse_at_kind` to return `kind::key` for `@key`
- [ ] Add `emit_at_key` opcode (opcode 21)
- [ ] Update `bc_compiler::emit_at_var` add `case kind::key → emit_at_key`
- [ ] Update `bc_executor` add `L_emit_at_key`: `serialize_value(out, loop_->key)` (both paths)
- [ ] Update `ct_render_at_var` add `case ct_at_var_kind::key`
- [ ] `ct_parse.hpp`: add key case in at_var switch
- [ ] `resolve.hpp`: add `@key` handling in `resolve_value` and `evaluate_if_expr`
- [ ] Add `std::string_view key{}` to `loop_state` and `bc_loop_state`
- [ ] Copy, build, benchmark

### Task 4: Struct-field iteration

**Files:**
- Modify: `include/injamm/detail/bytecode_exec.hpp`
- Modify: `include/injamm/detail/ct_render.hpp`

- [ ] **Bytecode VM**: In `L_emit_section` (and switch path), after `ct_is_vector_like<FT>` and `std::same_as<FT, bool>` branches, add:
  ```cpp
  else if constexpr (ct_glz_reflectable<FT>) {
      constexpr auto sz = glz::reflect<FT>::size;
      auto tied = glz::to_tie(field);
      bc_loop_state ls;
      ls.count = sz;
      [&]<std::size_t... I>(std::index_sequence<I...>) {
          (([&] {
              ls.key = glz::reflect<FT>::keys[I];
              using elem_t = std::remove_cvref_t<decltype(glz::get<I>(tied))>;
              bc_executor<elem_t, RootT> child_exec(bc_, glz::get<I>(tied), root_value_, &ls, out_);
              auto r2 = child_exec.execute_impl(pc + 1, body_end - 1);
              if (!r2) { res = r2; return; }
          }()), ...);
      }(std::make_index_sequence<sz>{});
  }
  ```
- [ ] **NTTP path**: Same branch in `ct_render_section` after `ct_is_vector_like` and `bool`:
  ```cpp
  else if constexpr (ct_glz_reflectable<FT>) {
      constexpr auto sz = glz::reflect<FT>::size;
      auto tied = glz::to_tie(field);
      loop_state ls;
      ls.count = sz;
      [&]<std::size_t... I>(std::index_sequence<I...>) {
          (([&] {
              ls.key = glz::reflect<FT>::keys[I];
              res = ct_render_chunks<Mode>(out, chunks, body_start, body_end, glz::get<I>(tied), root_value, &ls);
          }()), ...);
      }(std::make_index_sequence<sz>{});
  }
  ```
- [ ] Copy, build, benchmark

### Task 5: Tests + final verification

**Files:**
- Modify: `tests/test_injamm.cpp`

- [ ] Add tests for @root, @root.field, @key, struct-field iteration
- [ ] Build and run injamm tests
- [ ] Rebuild benchmarks
- [ ] Run final benchmarks
- [ ] Compare with baseline (check for regressions)
