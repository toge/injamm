# Issue #11 AOT Parity Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `injamm_codegen` match VM behavior for comparison operands, lambda-nested loop control, and string sections while retaining the value-to-root fallback already fixed by #10.

**Architecture:** Add a VM-shaped comparison helper and complete comparison access generation, then add section-local control state only to sections containing `break`/`continue`, and finally classify string sections before all loop fast paths. Use generated-code live tests as the primary TDD boundary: every new case is generated, C++-compiled, executed, and compared with the VM.

**Tech Stack:** C++23 templates and concepts, CMake/vcpkg, Glaze reflection, `injamm_codegen`, custom live parity tests, Catch2 regressions, GCC plus available Clang verification.

---

## File Map

- `CMakeLists.txt`: register generated live-test headers for each Issue #11 regression.
- `test_codegen/codegen_case_types.hpp`: add root and item data needed by comparison/control/string cases.
- `test_codegen/test_codegen_live.cpp`: include generated headers and compare codegen output with VM output.
- `test_codegen/codegen_helpers.hpp`: add string-section traits and VM-shaped typed comparison.
- `util/injamm_codegen.cpp`: collect comparison/control metadata, generate complete access and comparison expressions, and emit control-aware section code.
- `test_codegen/test_codegen_badbc.cpp`: reject incomplete compare metadata and assert generated code no longer contains the silent-zero fallback.
- `test_codegen/render*.hpp`, `examples/codegen_bench/render_*.hpp`: regenerate with the fixed generator/helper.
- `README.md`, `SYNTAX.md`, `CHANGELOG.md`: document current AOT behavior and the fix.

Do not modify `include/injamm/bytecode_exec.hpp`, `include/injamm/bytecode_compile.hpp`, bytecode serialization, or public syntax. Those define the VM oracle for this change.

## Baseline Confirmed After #10 Merge

On `main` after PR #12:

- filtered loop value-to-root fallback compiles and matches VM;
- index loop value-to-root fallback compiles and matches VM;
- `if` LHS value-to-root fallback compiles and matches VM;
- break/continue inside generated section lambdas is still invalid C++;
- variable RHS comparison silently compares against `0`;
- string-literal RHS and LHS `this` generate invalid C++;
- normal and filtered string sections iterate characters rather than executing once.

The two-stage access regressions in the passing group must still be added to the live matrix even when no implementation change is required.

---

### Task 1: Add the Comparison and Access Parity Matrix, Then Fix RHS Generation

**Files:**
- Modify: `test_codegen/codegen_case_types.hpp`
- Modify: `CMakeLists.txt`
- Modify: `test_codegen/test_codegen_live.cpp`
- Modify: `test_codegen/codegen_helpers.hpp`
- Modify: `util/injamm_codegen.cpp`
- Modify: `test_codegen/test_codegen_badbc.cpp`

- [ ] **Step 1: Add explicit comparison/control data to `CaseData`**

Keep the existing fields and append only test data needed by the generated cases:

```cpp
enum class CaseLevel : int { none, ready };

struct CaseItem {
  // existing fields...
  int count = 3;
};

struct CaseData {
  // existing fields...
  int                       qty       = 3;
  int                       limit     = 3;
  double                    ratio     = 1.5;
  double                    ceiling   = 2.5;
  std::string               alias     = "R";
  std::string               numeric   = "30";
  std::string               invalid_numeric = "not-a-number";
  std::string               empty;
  std::optional<int>        opt_limit = 3;
  CaseLevel                 level     = CaseLevel::ready;
  CaseLevel                 other_level = CaseLevel::ready;
  std::vector<std::string>  tags      = {"t1", "t2", "t3"};
};
```

Use aggregate Glaze reflection for the new fields; do not add unrelated metadata infrastructure.

- [ ] **Step 2: Register comparison and already-fixed access cases in CMake**

Add `add_codegen_case(...)` entries beside the existing section cases:

```cmake
add_codegen_case(if_cmp_variable      "{{#if qty == limit}}Y{{else}}N{{/if}}")
add_codegen_case(if_cmp_ne            "{{#if qty != limit}}Y{{else}}N{{/if}}")
add_codegen_case(if_cmp_gt            "{{#if qty > 2}}Y{{else}}N{{/if}}")
add_codegen_case(if_cmp_gte           "{{#if qty >= 3}}Y{{else}}N{{/if}}")
add_codegen_case(if_cmp_lt            "{{#if qty < 4}}Y{{else}}N{{/if}}")
add_codegen_case(if_cmp_lte           "{{#if qty <= 3}}Y{{else}}N{{/if}}")
add_codegen_case(if_cmp_string_var    "{{#if name == alias}}Y{{else}}N{{/if}}")
add_codegen_case(if_cmp_string        "{{#if name == \"R\"}}Y{{else}}N{{/if}}")
add_codegen_case(if_cmp_empty_string  "{{#if name == \"\"}}Y{{else}}N{{/if}}")
add_codegen_case(if_cmp_this          "{{#tags}}{{#if this == \"t1\"}}Y{{else}}N{{/if}}{{/tags}}")
add_codegen_case(if_cmp_loop_index    "{{#items}}{{#if loop.index == 0}}F{{else}}N{{/if}}{{/items}}")
add_codegen_case(if_cmp_loop_index1   "{{#items}}{{#if loop.index1 == 1}}F{{else}}N{{/if}}{{/items}}")
add_codegen_case(if_cmp_loop_size     "{{#items}}{{#if loop.size == 2}}S{{else}}N{{/if}}{{/items}}")
add_codegen_case(if_cmp_root_fallback "{{#items}}{{#if qty == limit}}Y{{else}}N{{/if}}{{/items}}")
add_codegen_case(if_cmp_lhs_root_rhs_value "{{#items}}{{#if qty == count}}Y{{else}}N{{/if}}{{/items}}")
add_codegen_case(if_cmp_lhs_value_rhs_root "{{#items}}{{#if on == active}}Y{{else}}N{{/if}}{{/items}}")
add_codegen_case(if_cmp_numeric       "{{#if numeric == age}}Y{{else}}N{{/if}}")
add_codegen_case(if_cmp_invalid_numeric "{{#if invalid_numeric == age}}Y{{else}}N{{/if}}")
add_codegen_case(if_cmp_numeric_float "{{#if numeric == ratio}}Y{{else}}N{{/if}}")
add_codegen_case(if_cmp_arithmetic_string "{{#if qty == alias}}Y{{else}}N{{/if}}")
add_codegen_case(if_cmp_float         "{{#if ratio >= ceiling}}Y{{else}}N{{/if}}")
add_codegen_case(if_cmp_optional_rhs  "{{#if qty == opt_limit}}Y{{else}}N{{/if}}")
add_codegen_case(if_cmp_optional_lhs  "{{#if opt_limit == qty}}Y{{else}}N{{/if}}")
add_codegen_case(if_cmp_enum          "{{#if level == 1}}Y{{else}}N{{/if}}")
add_codegen_case(if_cmp_enum_var      "{{#if level == other_level}}Y{{else}}N{{/if}}")
add_codegen_case(loop_filter_fallback "{{#tags}}{{#items | take(1)}}{{name}}{{/items}}{{/tags}}")
add_codegen_case(loop_index_fallback  "{{#tags}}{{#items}}{{loop.index}}{{name}}{{/items}}{{/tags}}")
add_codegen_case(if_root_fallback     "{{#items}}{{#if active}}T{{else}}F{{/if}}{{/items}}")
add_codegen_case(if_not_root_fallback "{{#items}}{{#if !active}}F{{else}}T{{/if}}{{/items}}")
add_codegen_case(if_and_root_fallback "{{#items}}{{#if active && age}}A{{else}}B{{/if}}{{/items}}")
add_codegen_case(if_or_root_fallback  "{{#items}}{{#if active || age}}A{{else}}B{{/if}}{{/items}}")
add_codegen_case(if_filtered_fallback "{{#items}}{{#if age | gt(18)}}A{{else}}B{{/if}}{{/items}}")
```

In `test_codegen_live.cpp`, include every generated `case_*.hpp`, create `d2`/small derived datasets with the required true/false values, and call `live_check` for every template. Use a separate `d_empty` for the empty-string literal case so existing `items.empty()` assumptions remain unchanged. For `CaseItem::count`, use a derived dataset whose count differs from root `qty` to exercise the false side of value/root resolution.

- [ ] **Step 3: Add generator-output and malformed-bytecode assertions**

In `test_codegen_badbc.cpp`, add:

1. a `run_forbidden_case` helper that requires `injamm_codegen -e` to succeed and asserts a forbidden substring is absent;
2. valid cases proving the generated headers no longer contain `data.qty == 0`, `data.name == 0`, or `_item1.this`;
3. a compact v6 `.bc` builder with the complete layout:
   - `IJBC`, `u32(6)`, body `is_simple=u8(0)`, `literal_total_size=u64(0)`;
   - instruction count `u64(2)`;
   - instruction 0: `emit_if_eq=u8(10)`, jump `u32(2)`, var-ref `u32(0)`, operand3 `u32(0)`;
   - instruction 1: `emit_endif=u8(17)`, operand1 `u32(0)`, operand2 `u32(0)`, operand3 `u32(0)`;
   - literal count `u64(0)`;
   - var-ref count `u64(1)`, followed by string `key="age"`, `has_dot=u8(0)`, `is_loop_parent=u8(0)`, compare kind `u8`, RHS string, `rhs_has_dot=u8(0)`, `filter_flags=u8(0)`, `section_op_count=u8(0)`, string-filter count `u64(0)`, integer-filter count `u64(n)`, optional integer-filter entries (`u8 kind`, `i32 arg`), float-filter count `u64(0)`, `root_fallback=u8(0)`;
   - partial count `u64(0)`;
4. rejection cases for:
   - `compare_operand_kind::int_literal` with no integer filter,
   - `compare_operand_kind::variable` with an empty RHS key.

Do not reject `compare_operand_kind::string_literal` with empty `compare_rhs_text`. Because `run_ok_case` interpolates the expression into a double-quoted shell argument, pass the empty-literal expression from C++ as `{{#if name == \\\"\\\"}}` (runtime backslash-escaped quotes) or replace that helper with argv-safe invocation. Add a successful generation assertion so the empty literal reaches codegen as `""`, not as a stripped shell token.

- [ ] **Step 4: Run the comparison build and verify the pre-fix failure**

Run:

```bash
cmake --build build --target test_codegen_live -j2
```

Expected before implementation: failure while compiling one or more generated `case_if_cmp_*.hpp` files. Record that the failure comes from a literal/variable RHS becoming `0` or from `_item...this`; do not proceed with a different change if the failure has another cause.

- [ ] **Step 5: Generate RHS expressions from compare metadata**

In `code_generator` in `util/injamm_codegen.cpp`:

- replace `get_compare_value()` with `get_compare_rhs_expression(bc::var_ref const&)`;
- return an integer expression only for `int_literal`, a `cpp_string(...)` expression for `string_literal`, and a resolved access for `variable`;
- when `compare_rhs_kind == none` and exactly one existing integer-filter compatibility payload exists, retain that legacy integer only;
- reject all other missing `int_literal`, empty `variable`, or unknown-kind payloads before emitting a header;
- add RHS keys to `collect_two_stage_keys`, using value-first/root-second resolution in loops and partials.

Add a `validate_compare_ref(...)` guard in `reader::validate_bytecode` so malformed `.bc` fails before `generate()`.

- [ ] **Step 6: Resolve special LHS comparisons and route all operators through a helper**

Extend `resolve_if_access` (or split it into a clearly named special/field resolver) so:

- `this` resolves to `value_expr()`;
- `loop.index`, `loop.index1`, and `loop.size` resolve to the current generated loop values;
- unsupported special comparison LHS values outside a loop generate a false condition rather than a member named `this` / `loop`;
- ordinary field LHS values continue to use `resolve_value_access`.

Extend the same metadata-context prepass used in Task 3 to inspect all six comparison opcodes as well as plain `emit_if`. When the comparison LHS is `loop.index`, `loop.index1`, or `loop.size`, record the `loop-context` entry that owns it and mark that section's **container category** for indexed emission. The body expression itself must use the passed context, so the runtime scalar/string branch can supply inherited/guard metadata without depending on whether the container branch was indexed. `precompute_index_loops` and expression emission must consume the same context entry; do not maintain two unrelated depth counters.

Replace direct `lhs <op> rhs` output for the six compare opcodes with:

```cpp
if (codegen_compare(injamm::detail::bc_opcode::emit_if_eq,
                    injamm::detail::compare_operand_kind::variable,
                    lhs, rhs)) {
```

Use the actual opcode for each operator. Keep `{{#if a}}`, `!`, `&&`, and `||` truthiness generation separate.

- [ ] **Step 7: Add the VM-shaped comparison helper**

In `test_codegen/codegen_helpers.hpp`, add:

```cpp
template <class L, class R>
constexpr bool codegen_compare(
  injamm::detail::bc_opcode op,
  injamm::detail::compare_operand_kind rhs_kind,
  const L& lhs,
  const R& rhs);
```

Implement it with `if constexpr` and these exact rules:

1. optional RHS unwraps only when it has a value;
2. optional LHS evaluates false;
3. enum values compare through their underlying type;
4. for arithmetic LHS and variable RHS, integral RHS uses integer comparison and floating RHS uses double comparison;
5. enum LHS follows VM behavior: variable integral/enum RHS compares underlying integers, while a variable floating RHS uses the VM's resolved integer payload;
6. string LHS plus integer literal or variable numeric RHS parses the complete LHS with `from_chars`; partial input/trailing characters evaluate false;
7. string LHS plus variable floating RHS follows the VM's current `rhs_num` path exactly; do not silently substitute `static_cast<long long>(rhs)` in a way that changes this behavior;
8. string/string and char-pointer/string-like pairs support only `==` / `!=`;
9. a string variable RHS does not convert an arithmetic/enum LHS to numeric;
10. string-literal RHS keeps the literal kind in the helper call; never infer “literal vs variable” only from C++ type;
11. invalid conversion, unresolved value, unsupported order comparison, or missing optional value evaluates false.

Use `std::string_view`, `char_pointer_v`, and `from_chars` facilities already available in the helper. Do not allocate for ordinary numeric/string comparisons. Add explicit comments only where VM's asymmetric integer/floating behavior would otherwise be “simplified” incorrectly.

- [ ] **Step 8: Run targeted comparison/access tests**

Run:

```bash
cmake --build build --target test_codegen_live test_codegen_badbc -j2
./build/test_codegen_live
ctest --test-dir build -R '^(codegen_live_test|codegen_badbc_test)$' --output-on-failure
```

Expected: all generated comparison/access cases pass for both `d` and the alternate datasets, and no generated header contains `== 0` for variable/string RHS.

- [ ] **Step 9: Commit comparison/access parity**

```bash
git add util/injamm_codegen.cpp test_codegen/codegen_helpers.hpp \
  test_codegen/codegen_case_types.hpp test_codegen/test_codegen_live.cpp \
  test_codegen/test_codegen_badbc.cpp CMakeLists.txt
git commit -m "fix: preserve codegen comparison operands"
```

Include the repository's `Co-authored-by` trailer.

---

### Task 2: Add Loop-Control Live Cases and Propagate Lambda Flags

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `test_codegen/test_codegen_live.cpp`
- Modify: `util/injamm_codegen.cpp`

- [ ] **Step 1: Register control-flow cases**

Add these generated cases:

```cmake
add_codegen_case(control_break
  "{{#items}}{{name}}{{#break}}{{/items}}")
add_codegen_case(control_continue
  "{{#items}}{{#continue}}{{name}}{{/items}}")
add_codegen_case(control_scalar_break
  "{{#items}}{{#on}}{{#break}}{{/on}}{{name}}{{/items}}")
add_codegen_case(control_inverted_continue
  "{{#items}}{{^on}}{{#continue}}{{/on}}{{name}}{{/items}}")
add_codegen_case(control_if_break
  "{{#items}}{{name}}{{#if on}}{{#break}}{{/if}}{{/items}}")
add_codegen_case(control_indexed_continue
  "{{#tags}}{{loop.index}}{{#if this == \"t2\"}}{{#continue}}{{/if}}{{this}}{{/tags}}")
add_codegen_case(control_join_break
  "{{#tags | join(\",\")}}{{this}}{{#if this == \"t2\"}}{{#break}}{{/if}}{{/tags}}")
add_codegen_case(control_join_continue
  "{{#tags | join(\",\")}}{{this}}{{#if this == \"t2\"}}{{#continue}}{{/if}}{{/tags}}")
add_codegen_case(control_take_continue
  "{{#tags | take(2)}}{{#if this == \"t1\"}}{{#continue}}{{/if}}{{this}}{{/tags}}")
add_codegen_case(control_reverse_break
  "{{#tags | reverse}}{{this}}{{#if this == \"t2\"}}{{#break}}{{/if}}{{/tags}}")
add_codegen_case(control_stride_continue
  "{{#tags | stride(1,1)}}{{#if this == \"t1\"}}{{#continue}}{{/if}}{{this}}{{/tags}}")
add_codegen_case(control_nested
  "{{#items}}{{#active}}{{#continue}}{{/active}}{{name}}{{/items}}")
add_codegen_case(control_optional_break
  "{{#items}}{{#opt}}{{#break}}{{/opt}}{{name}}{{/items}}")
add_codegen_case(control_number_continue
  "{{#tags}}{{#age}}{{#continue}}{{/age}}{{this}}{{/tags}}")
add_codegen_case(control_enum_break
  "{{#tags}}{{#level}}{{#break}}{{/level}}{{this}}{{/tags}}")
add_codegen_case(control_section_else_continue
  "{{#items}}{{#on}}{{name}}{{else}}{{#continue}}{{/on}}X{{/items}}")
add_codegen_case(control_inverted_break
  "{{#items}}{{^on}}{{#break}}{{/on}}{{name}}{{/items}}")
add_codegen_case(control_syntax_example
  "{{#tags}}{{this}}{{#if loop.is_last}}.{{else}}{{#break}}{{/if}}{{/tags}}")
```

Add matching `live_check` calls. Keep all expected strings VM-derived; do not hard-code codegen expectations separately.

- [ ] **Step 2: Verify the pre-fix compile failure**

Run:

```bash
cmake --build build --target test_codegen_live -j2
```

Expected: direct `break` / `continue` appears inside a generated lambda and GCC reports that it is outside a loop. Stop if the failure is instead from an unrelated case.

- [ ] **Step 3: Precompute opcode-to-loop-state control ownership**

Add a block-aware prepass in `code_generator`. Model the VM `bc_loop_state` rules explicitly instead of attaching each opcode to “the nearest lambda”:

- an ordinary `emit_section` main body has a potential new loop state only when the runtime field is a container or a string guard; bool/number/enum/optional do not create a new state;
- `emit_inverted`, `if`, logical `if`, and `if_filtered` do not create loop state;
- a section `else` runs after the section state is absent, so its controls target the enclosing state;
- string sections create an isolated guard state;
- a nested section may run multiple runtime categories, so the body receives local flags and the section dispatch decides whether to consume or propagate them.

For every control opcode, emit a stable control ID and record:

- the syntactic owner section;
- the caller/lambda boundary whose `return` stops the remaining body;
- whether control occurs in a main body, `else`, or nested body;
- the enclosing potential VM state to receive propagated flags;
- whether the section is emitted through a lambda or the filtered/index inline loop path.

Extend `open_block` with control identifiers/flags rather than using global flags shared by all loops.

Represent generated control with a single shared slot per potential section loop state:

```cpp
struct _cg_control_state {
  bool break_requested = false;
  bool continue_requested = false;
};
```

Declare the slot in the scope that encloses every mutually-exclusive section-dispatch branch. Every main-body lambda that belongs to that section accepts or captures the same slot by reference; the optional/scalar/string/container branches do not own separate copies. A nested section receives a pointer/reference to its own slot plus the enclosing slot. The section `else` lambda receives the enclosing slot directly because the section itself did not enter a VM loop state. `if` / logical / filtered blocks do not allocate a slot and continue using the active slot passed through their enclosing section.

On a control opcode, set only the active slot and return from the lambda that currently bounds the remaining body. Do not represent the target with a global “current section” variable. At section end, the dispatch branch consumes the shared slot or copies it into the supplied enclosing slot according to the runtime category.

- [ ] **Step 4: Replace lambda-local control statements**

For control-bearing lambda bodies:

```cpp
emit_break      -> _cg_break_N = true; return;
emit_continue   -> _cg_continue_N = true; return;
```

For control-bearing filtered/index loop bodies, generate a local body lambda for that section instead of emitting raw body statements into the loop. This avoids `return` escaping the whole render function and allows the same flag protocol on every loop form.

Do not add flags or wrapper lambdas to templates with no control opcodes.

- [ ] **Step 5: Consume and propagate flags in VM order**

At each section dispatch:

- container branch: reset flags per iteration, invoke body, consume `continue` first, then `break`;
- bool/number/enum/optional branch: propagate flags to the enclosing section after the body returns, because VM executes with the same `bc_loop_state`;
- inverted branch: propagate flags to the enclosing section;
- `else` branch: target the enclosing loop state, not the false section that did not execute;
- nested `if` / `if_filtered` / logical blocks do not consume flags; only the section caller at their enclosing boundary performs propagation;
- top-level with no loop state: stop the current generated lambda/body but never emit a bare invalid `break`/`continue`.

For filtered loops with `join`, move separator emission after the successful body and after control checks. Advance stride source positions exactly once even on `continue`; break must skip join output.

Validate the model with the ordinary, indexed, filtered, reverse, stride, scalar, enum, optional, inverted, else, join, and nested cases above. A flag may be consumed once and only at a VM loop-state boundary; section-lambda `return` alone is not treated as consuming it.

- [ ] **Step 6: Run loop-control and full live tests**

Run:

```bash
cmake --build build --target test_codegen_live -j2
./build/test_codegen_live
ctest --test-dir build -R '^codegen_live_test$' --output-on-failure
```

Expected: every VM/codegen pair matches, including the exact SYNTAX.md break example and filtered join behavior.

- [ ] **Step 7: Commit loop-control parity**

```bash
git add util/injamm_codegen.cpp test_codegen/test_codegen_live.cpp CMakeLists.txt
git commit -m "fix: propagate codegen loop control through lambdas"
```

---

### Task 3: Make String Sections Execute Once on Every Loop Path

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `test_codegen/codegen_case_types.hpp`
- Modify: `test_codegen/test_codegen_live.cpp`
- Modify: `test_codegen/codegen_helpers.hpp`
- Modify: `util/injamm_codegen.cpp`

- [ ] **Step 1: Add string-like section data**

Append to `CaseData`:

```cpp
std::string_view short_text = "SV";
const char*      text_ptr   = "PTR";
```

Keep string-view targets backed by a static string literal so copied datasets cannot dangle.

- [ ] **Step 2: Register ordinary, filtered, indexed, and guard-boundary string cases**

Add:

```cmake
add_codegen_case(string_section
  "{{#items}}{{#name}}[{{name}}]{{/name}}{{/items}}")
add_codegen_case(string_this_section
  "{{#items}}{{#name}}[{{this}}]{{/name}}{{/items}}")
add_codegen_case(string_filtered_section
  "{{#items}}{{#name | take(1)}}[{{this}}]{{/name}}{{/items}}")
add_codegen_case(string_index_section
  "{{#items}}{{#name}}{{loop.index}}:{{loop.index1}}:{{loop.is_first}}:{{loop.is_last}}:{{loop.size}}:{{this}}{{/name}}{{/items}}")
add_codegen_case(string_at_section
  "{{#items}}{{#name}}{{#loop.is_first}}F{{/loop.is_first}}{{^loop.is_last}}N{{/loop.is_last}}{{/name}}{{/items}}")
add_codegen_case(string_reverse_section
  "{{#items}}{{#name | reverse}}[{{name}}]{{/name}}{{/items}}")
add_codegen_case(string_stride_section
  "{{#items}}{{#name | stride(1,1)}}[{{name}}]{{/name}}{{/items}}")
add_codegen_case(string_view_section
  "{{#short_text}}[{{this}}]{{/short_text}}")
add_codegen_case(string_ptr_section
  "{{#text_ptr}}[{{this}}]{{/text_ptr}}")
add_codegen_case(string_control_guard
  "{{#items}}{{#name}}{{#break}}{{name}}{{/name}}X{{/items}}")
add_codegen_case(string_continue_guard
  "{{#items}}{{#name}}{{#continue}}{{name}}{{/name}}Y{{/items}}")
add_codegen_case(string_take_control_guard
  "{{#items}}{{#name | take(1)}}{{#break}}{{name}}{{/name}}T{{/items}}")
add_codegen_case(string_reverse_control_guard
  "{{#items}}{{#name | reverse}}{{#continue}}{{name}}{{/name}}R{{/items}}")
add_codegen_case(string_stride_control_guard
  "{{#items}}{{#name | stride(1,1)}}{{#break}}{{name}}{{/name}}S{{/items}}")
```

Create `d_empty_names` with every `CaseItem::name` empty and dataset variants with `short_text={}` / `text_ptr=nullptr`; call `live_check` for both non-empty and empty forms.

- [ ] **Step 3: Verify the pre-fix mismatch**

Run:

```bash
cmake --build build --target test_codegen_live -j2
./build/test_codegen_live
```

Expected before implementation: generated output shows per-character traversal (`97`, `98`, etc.) or invalid member access on `char`; VM executes the string body once with the parent scope.

- [ ] **Step 4: Add a normalized string-section trait**

In `test_codegen/codegen_helpers.hpp`, add `codegen_string_section_v<V>` using `std::remove_cvref_t` and covering:

- `std::string`;
- `std::string_view`;
- `const char*` / pointer forms accepted by `char_pointer_v`.

Keep `codegen_scalar_section_v` for bool/number/enum and keep optional separate. Dispatch optional, string, scalar, container in that explicit order.

- [ ] **Step 5: Classify sections before every loop fast path**

In `emit_instruction(emit_section, ...)`, make the first decision the type trait dispatch. The `section_op_count` and `index_loops_` branches may only be generated in the container branch; they must not emit `.size()`, `operator[]`, or range-for on a string value.

For a non-empty string section:

- execute the body once;
- keep `{{this}}` as the enclosing value, not as a character;
- create VM guard metadata (`active=true`, `index=0`, `size=0`) for generated loop expressions;
- ignore section filters that are meaningful only to an iterable;
- consume break/continue inside the guard without propagating to an enclosing container;
- evaluate direct loop opcodes and section-form loop conditions against the guard, including `emit_at_index`, `emit_at_index1`, `emit_at_first`, `emit_at_last`, `emit_at_size`, `emit_at_section`, and `emit_at_inverted`.

Define guard predicates exactly:

- `is_first = active && index == 0`;
- `is_last = active && size != 0 && index + 1 == size`;
- `at_last` / `if loop.is_last` therefore evaluate false for a string guard;
- `at_index` emits `0`, `at_index1` emits `1`, and `at_size` emits `0`.

Do not reuse expressions such as `_sizeN - 1` for the guard; the VM's `size=0` branch never underflows.

- [ ] **Step 6: Parameterize generated loop metadata used inside section lambdas**

Represent generated loop metadata with one consistent `_cg_loop_context` shape (`active`, `index`, `size`, and the enclosing context reference). Maintain a generator-side stack of syntactic loop contexts during the prepass and during emission:

- prepass entries correspond to `emit_section` bodies, record which body opcodes/refs need metadata, and mark only the container category for indexed C++ output;
- a runtime scalar/optional body receives the enclosing `_cg_loop_context`;
- a runtime string guard receives a new context `{active=true,index=0,size=0}`;
- a runtime container body receives the actual emitted index and kept count;
- nested sections push/pop their own selected context in generation, so a scalar nested body does not accidentally inherit a string guard declaration or lexical `_iN`.

Where an `emit_section` body references `loop.index`, `loop.index1`, `loop.size`, `loop.is_first`, `loop.is_last`, `emit_at_*`, `emit_at_section`, or `emit_at_inverted`, pass this context through the body lambda (or an equivalent dependent expression) instead of relying solely on lexically named `_iN/_sizeN` variables. Reuse the same metadata-context resolver for plain `if`, comparison LHS, direct interpolation, and section-form loop conditions.

Required call values:

- container: actual emitted index and kept count;
- string guard: `index=0`, `size=0`, `has_loop=true`;
- bool/number/enum/optional: enclosing loop metadata unchanged;
- top-level scalar without an enclosing loop: `has_loop=false`.

Use this same resolver for ordinary truthiness and comparison LHS paths so `loop.*` is not emitted as a field member.

- [ ] **Step 7: Verify all string paths**

Run:

```bash
cmake --build build --target test_codegen_live test_codegen_nosimd -j2
./build/test_codegen_live
ctest --test-dir build -R '^(codegen_live_test|codegen_nosimd_test)$' --output-on-failure
```

Expected: normal, filtered, loop-variable, `string_view`, and C-string sections match VM; break in a string guard stops that body but still renders the trailing outer-loop `X`.

- [ ] **Step 8: Commit string-section parity**

```bash
git add util/injamm_codegen.cpp test_codegen/codegen_helpers.hpp \
  test_codegen/codegen_case_types.hpp test_codegen/test_codegen_live.cpp CMakeLists.txt
git commit -m "fix: execute codegen string sections once"
```

---

### Task 4: Lock the Existing Two-Stage Access Paths and Fix `emit_var_size`

**Files:**
- Modify: `test_codegen/test_codegen_badbc.cpp`
- Modify: `test_codegen/test_codegen_live.cpp`
- Modify: `CMakeLists.txt`
- Modify: `util/injamm_codegen.cpp` only if a passing-baseline case regresses

- [ ] **Step 1: Confirm the live matrix covers every #10-shared access route**

Ensure the CMake/test matrix from Task 1 contains:

- filtered loop target fallback;
- index-dependent loop target fallback;
- plain `if`;
- `if !`;
- `if &&` / `if ||`;
- filtered `if`;
- comparison LHS and RHS fallback;
- `{{field.size}}` emitted as `emit_var_size`;
- partial body access already covered by existing partial live tests.

For the logical case, use `root_only`-style keys that do not exist on `CaseItem`.

Add a generated case:

```cmake
add_codegen_case(var_size_root_fallback
  "{{#items}}{{tags.size}}{{/items}}")
```

This deliberately places the `items` section reference before the root-only `tags` reference so the var-ref index is nonzero.

- [ ] **Step 2: Run these cases against current helper generation**

```bash
cmake --build build --target test_codegen_live -j2
./build/test_codegen_live
```

Expected on the #10-merged baseline: ordinary filtered/indexed/if paths already pass. The `var_size_root_fallback` case should fail because `bytecode_compile` stores the ref index in `emit_var_size.operand`, while current codegen reads `operand2` (which is normally zero). Fix `util/injamm_codegen.cpp` to read `bc.var_refs[inst.operand]`, then reuse `emit_value_ref` / `resolve_value_access`. Keep `reader::validate_bytecode` and `collect_two_stage_keys` on the same operand convention.

- [ ] **Step 3: Strengthen generated-text assertions**

In `test_codegen_badbc.cpp`, assert representative generated templates:

- use `cg_get_*` for a loop element's missing root key;
- do not contain `_itemN.<root-only-key>` for the fallback path;
- use `codegen_compare` for variable/string RHS;
- use `codegen_string_section_v` in section output;
- use `value_size(cgns_...cg_get_...` for root-fallback `emit_var_size`;
- retain direct `range-for` generation for ordinary container sections.

- [ ] **Step 4: Run codegen unit targets**

```bash
cmake --build build --target test_codegen test_codegen_ext test_codegen_nosimd \
  test_codegen_badbc test_codegen_live -j2
ctest --test-dir build -R '^codegen_.*|^ct_if_test$' --output-on-failure
```

Expected: all AOT tests pass, including legacy static generated headers and `--no-simd`.

- [ ] **Step 5: Commit access regression coverage**

```bash
git add CMakeLists.txt test_codegen/test_codegen_live.cpp \
  test_codegen/test_codegen_badbc.cpp
if ! git diff --quiet -- util/injamm_codegen.cpp; then
  git add util/injamm_codegen.cpp
fi
git commit -m "test: cover codegen issue 11 access paths"
```

---

### Task 5: Regenerate Fixtures, Update Documentation, and Verify the Branch

**Files:**
- Regenerate: `test_codegen/render*.hpp`
- Regenerate: `examples/codegen_bench/codegen_helpers.hpp`, `examples/codegen_bench/render_*.hpp`
- Modify: `README.md`
- Modify: `SYNTAX.md`
- Modify: `CHANGELOG.md`

- [ ] **Step 1: Rebuild the final generator and regenerate checked-in output**

```bash
cmake --build build --target injamm_codegen -j2
test_codegen/regenerate.sh ./build/injamm_codegen
examples/codegen_bench/gen.sh ./build/injamm_codegen
git --no-pager diff --check
```

Review generated diffs for:

- compare helpers replacing `== 0`;
- string-section trait/classification branches;
- control flags only in control-bearing templates;
- unchanged ordinary cases except output-shape differences required by the fixes.

- [ ] **Step 2: Update README**

In the `injamm_codegen` capability paragraph:

- state that strings/string views/C strings execute a non-empty section once and are never char-iterated;
- state that integer, string, and variable comparison RHS work in generated code;
- state that loop control works through normal/filtered/inverted section lambdas and retains VM behavior.

Keep the existing two-stage access and unsupported `loop.key` statements accurate.

- [ ] **Step 3: Update SYNTAX examples**

Update §5 to clarify one-time string section scope, §6.3 to include string-literal/variable RHS and LHS `this`, and §7 to state that the documented break/continue examples apply equally to `engine`, NTTP, and AOT generation. Include sample outputs already exercised by live tests.

- [ ] **Step 4: Update CHANGELOG**

At the top under the 2026-11-25 date, add one concise Issue #11 bullet covering:

- no silent RHS `0`;
- two-stage loop/if access parity;
- lambda-safe break/continue and join;
- one-time string sections.

- [ ] **Step 5: Run complete GCC verification**

```bash
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

Expected: all configured tests pass. If a target is memory-heavy, reduce CMake build parallelism rather than skipping it.

- [ ] **Step 6: Run Clang live-codegen verification when available**

```bash
cmake -B build-clang -S . \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_TOOLCHAIN_FILE="$HOME/vm/vcpkg/scripts/buildsystems/vcpkg.cmake" \
  -DBUILD_UTIL=ON -DBUILD_TEST=ON -DBUILD_EXAMPLE=OFF
cmake --build build-clang --target test_codegen_live -j2
ctest --test-dir build-clang -R '^codegen_live_test$' --output-on-failure
```

Do not rely on GCC-only acceptance; the generated `cg_get_*` function templates must continue to defer discarded members on Clang. Record MSVC as CI-required because it is unavailable locally.

- [ ] **Step 7: Inspect final branch diff and commit**

```bash
git --no-pager diff --check
git --no-pager status --short
git --no-pager diff --stat
```

Commit regenerated fixtures and documentation:

```bash
git add test_codegen/render*.hpp examples/codegen_bench/codegen_helpers.hpp \
  examples/codegen_bench/render_*.hpp README.md SYNTAX.md CHANGELOG.md
git commit -m "docs: document codegen parity fixes"
```

- [ ] **Step 8: Run final acceptance probes**

Run the complete test suite once after the documentation/fixture commit, and manually regenerate/compile these exact Issue #11 templates with `injamm_codegen`:

```bash
{{#items}}{{name}}{{#break}}{{/items}}
{{#tags}}{{#items | take(1)}}x{{/items}}{{/tags}}
{{#items}}{{#if active}}T{{/if}}{{/items}}
{{#if qty == limit}}Y{{else}}N{{/if}}
{{#if name == "R"}}Y{{else}}N{{/if}}
{{#tags}}{{#if this == "t1"}}Y{{/if}}{{/tags}}
{{#items}}{{#name}}[{{this}}]{{/name}}{{/items}}
```

Expected: every generated header compiles and its output equals VM output for representative truthy/falsy data. Stop only when all acceptance probes and the full suite pass.
