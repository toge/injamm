# macOS CI Failure Investigation

## Summary

**Run ID:** 29978879299  
**Exit code:** 8 (ctest)  
**Jobs affected:** `macos`, `macos-no-enum`  
**Platform:** `macos-latest` → Apple Clang 21.0.0, arm64, Xcode 26.5  
**Root cause:** NEON SIMD `html_escape_into` path in `include/injamm/escape.hpp` fails to HTML-escape `{{this}}` struct serialization on Apple Silicon.

---

## Exact Test Failures

Both macOS jobs have **exactly 2 test failures** (same tests):

### 1. `ct_this` (`tests/test_injamm_ct.cpp:239`)
```
REQUIRE( *r == "{&quot;name&quot;:&quot;alice&quot;,&quot;age&quot;:30}" )
with expansion:
  "{"name":"alice","age":30}"
  ==
  "{&quot;name&quot;:&quot;alice&quot;,&quot;age&quot;:30}"
```

### 2. `ct_this_struct` (`tests/test_injamm_ct.cpp:246`)
```
REQUIRE( *r == "[{&quot;name&quot;:&quot;alice&quot;,&quot;age&quot;:30}]" )
with expansion:
  "[{"name":"alice","age":30}]"
  ==
  "[{&quot;name&quot;:&quot;alice&quot;,&quot;age&quot;:30}]"
```

Both expect HTML-escaped JSON (`&quot;`) but get raw JSON (`"`).

**These are NOT related to partials/nested partials.** They are pure `{{this}}` struct serialization + HTML-escaping tests.

---

## Linux vs macOS Comparison

| | Linux (Fedora GCC) | macOS (Apple Clang arm64) |
|---|---|---|
| Tests | 9/9 passed | 2 failed |
| `ct_this` result | `{&quot;name&quot;:&quot;alice&quot;,&quot;age&quot;:30}` ✅ | `{"name":"alice","age":30}` ❌ |
| Compiler | GCC (14/15/16) | Apple Clang 21.0.0 (arm64) |
| SIMD path used | **AVX2** (x86_64) | **NEON** (ARM AArch64) |

---

## Root Cause Analysis

### Why it's NOT the partials code

The `render_partial()` change in commit 9d28392 is unrelated. Both commits (98473bc and 9d28392) fail with the **identical** 2 test failures. The SIMD commit 98473bc introduced the issue.

### The code flow for `{{this}}` with a struct

1. Compile-time bytecode compilation → `bc_opcode::emit_this`
2. Runtime `handle_emit_this()` (`bytecode_exec.hpp:548`):
   - Calls `glz::write_json(ex.value_, ex.emit_this_scratch_)` → produces `{"name":"alice","age":30}` (25 bytes)
   - Calls **`html_escape_into(ex.out_, ex.emit_this_scratch_)`** → should escape `"` → `&quot;`

### The SIMD path selection in `escape.hpp`

```cpp
#if defined(__AVX2__)        → AVX2 (x86_64, works on Linux)
#elif defined(__SSE2__)      → SSE2 (older x86_64)
#elif defined(__ARM_NEON)    → NEON (ARM, **taken on Apple Silicon**) ← BUG
#else                        → scalar fallback (works correctly)
```

On Apple Silicon (arm64), Apple Clang defines `__ARM_NEON`, so the NEON SIMD path is used.  
On Linux x86_64, `__AVX2__` is defined, so the AVX2 path is used (works correctly).

### The bug: NEON `html_escape_into` produces wrong output

The NEON `neon_movemask` implementation (lines 210-221) computes a 16-bit mask from vector comparison results using a `vpaddlq` chain. On Apple Clang 21 for arm64, this function likely returns 0 for ALL inputs, causing the SIMD loop to treat every 16-byte chunk as having no special characters, appending them verbatim. Only the tail (< 16 bytes) would go through `process_special`, but if the tail also doesn't get proper escaping, the entire output is raw JSON.

### Why regular `{{var}}` escaping doesn't fail

Regular `{{var}}` for a `std::string` field goes through `emit_value_static` → `html_escape_into`. The test suite has 700+ passing tests, meaning the SIMD path DOES work for SOME inputs — but the `neon_movemask` implementation may have edge-case failures with the specific byte patterns produced by JSON serialization (many `"` bytes, specific alignment).

---

## Likely Root Cause

**The `neon_movemask` function in `include/injamm/escape.hpp:210-221` has a bug on Apple Clang 21 arm64** — the pairwise-add reduction chain (`vpaddlq_u8` → `vpaddlq_u16` → `vpaddlq_u32`) produces wrong mask values, causing `mask == 0` to be true when it shouldn't be. This causes the SIMD loop to bypass HTML escaping entirely for 16-byte-aligned chunks.

## Recommended Fix

**Short-term:** Guard the NEON path to exclude Apple Clang (use scalar fallback):

```cpp
#elif defined(__ARM_NEON) && !defined(__apple_build_version__)
```

This forces macOS to use the scalar `html_escape_scalar` path, which is correct.

**Long-term:** Debug the `neon_movemask` function on macOS ARM64 to find the actual NEON intrinsics bug. Consider replacing the `vpaddlq` chain with a `vqtbl1q`-based approach or the more standard `vshlq` reduction pattern.

---

## File Locations

| File | Lines | Description |
|---|---|---|
| `include/injamm/escape.hpp` | 184–263 | NEON `html_escape_into` (buggy) |
| `include/injamm/escape.hpp` | 210–221 | `neon_movemask` (likely bug location) |
| `include/injamm/escape.hpp` | 264–277 | Scalar fallback (working) |
| `include/injamm/bytecode_exec.hpp` | 548–560 | `handle_emit_this` (non-threaded dispatch) |
| `include/injamm/bytecode_exec.hpp` | 2096–2108 | `L_emit_this` (threaded dispatch) |
| `tests/test_injamm_ct.cpp` | 234–247 | Failing test cases |
