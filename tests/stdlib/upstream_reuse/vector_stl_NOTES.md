# Porting Microsoft STL vector growth/invariant algorithms into C+ vector

Date: 2026-11-23. Branch `agent/modern-stdlib`.

Strict file ownership honoured for the port: only `stdlib/vector.hp`,
`tests/stdlib/upstream_reuse/vector_stl.cp` and this file were created/modified
by this agent. No git add/commit and no compiler/runtime edits were made.

## 1. Upstream source actually fetched (provenance)

All fetches were made with `webfetch` (raw text) on 2026-11-23:

| Purpose | Exact URL fetched | Result |
| --- | --- | --- |
| vector growth / guards | `https://raw.githubusercontent.com/microsoft/STL/main/stl/inc/vector` | full text, 4194 lines; tool saved remainder to a local tool-output file |
| `_Get_size_of_n` | `https://raw.githubusercontent.com/microsoft/STL/main/stl/inc/xmemory` | full text, 2848 lines |
| license | `https://raw.githubusercontent.com/microsoft/STL/main/LICENSE.txt` | full Apache-2.0 text + LLVM exceptions appendix |

**License.** `LICENSE.txt` opens with "The Microsoft C++ Standard Library is
under the Apache License v2.0 with LLVM Exception:" and ends with the section
"---- LLVM Exceptions to the Apache 2.0 License ----". Both `vector` and
`xmemory` carry the header lines `// Copyright (c) Microsoft Corporation.` and
`// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception`. Reuse is therefore
permissive; only short algorithm descriptions/quote-shaped predicate comments
were carried over, not vendored source.

**Revision/commit.** The raw `main` URLs return file bytes only: **no commit
hash is visible in the fetched text or headers**. The already-committed study
`docs/stdlib/upstream_study/microsoft_stl.md` records the pin
`0cabaf3c7e0a73fe3040aabce40cd8833e80392b` for the same source; that pin is
cited here as inherited context, not as a value read from today's fetch.

## 2. Exact upstream source lines inspected

From `stl/inc/vector` (line numbers in the fetched file):

- **1920–1923** `max_size()`:
  `constexpr auto _Diff_max = static_cast<size_type>(_STD _Max_limit<difference_type>()); return (_STD min)(_Diff_max, _Alty_traits::max_size(_Getal()));`
- **2009–2025** `_Calculate_growth(const size_type _Newsize)`:
  ```
  const size_type _Oldcapacity = capacity();
  const auto _Max              = max_size();
  if (_Oldcapacity > _Max - _Oldcapacity / 2) {
      return _Max; // geometric growth would overflow
  }
  const size_type _Geometric = _Oldcapacity + _Oldcapacity / 2;
  if (_Geometric < _Newsize) {
      return _Newsize; // geometric growth would be insufficient
  }
  return _Geometric; // geometric growth is sufficient
  ```
- **2186–2188** `_Xlength()` → `_Xlength_error("vector too long")`; the
  `_Oldsize == max_size()` guard that calls it is at **890–892** (and the
  subtract-form `_Count > max_size() - _Oldsize` guards at 975–977,
  1093–1095, 1185–1187, 3544).
- **1725–1734** `reserve()`: `_Newcapacity > max_size()` → `_Xlength()`, then
  `_Reallocate<_Reallocation_policy::_At_least>`; **1736–1748** `shrink_to_fit()`.
- **1639–1671** `_Reallocate`: allocate at 1651/1655, `_Allocation_guard` at
  1661, `_Guard._Ptr = nullptr;` at 1669 (guard released only on success),
  `_Change_array(...)` at 1670 (commit).
- **443–456** `_Allocation_guard` (destructor deallocates when `_Ptr` set) and
  **628–643** `_Reallocation_guard2` (destructor destroys constructed range and
  deallocates when `_New_begin` set).
- **1661 / 1669–1670**, **897–909**, **981–993** etc.: allocation and element
  construction happen before any container pointer/capacity field is published.

From `stl/inc/xmemory`:

- **111–122** `_Get_size_of_n<size_t _Ty_size>(const size_t _Count)`:
  ```
  constexpr bool _Overflow_is_possible = _Ty_size > 1;
  if constexpr (_Overflow_is_possible) {
      constexpr size_t _Max_possible = static_cast<size_t>(-1) / _Ty_size;
      if (_Count > _Max_possible) {
          _Throw_bad_array_new_length(); // multiply overflow
      }
  }
  return _Count * _Ty_size;
  ```
- **688, 702, 994**: call sites that feed `_Get_size_of_n<sizeof(value_type)>`
  into allocation, confirming it is the pre-multiply guard.

## 3. What was adapted, and what was deliberately NOT copied

Adapted (structure/invariants only, re-expressed in strict C+):

| Upstream algorithm | C+ adaptation in `stdlib/vector.hp` |
| --- | --- |
| `_Calculate_growth` subtraction-form overflow guard, geometric step, demand floor, saturation at the ceiling | `vector_calculate_growth(oldcapacity, newsize)`: `if (oldcapacity >= max_count) return max_count; if (oldcapacity > max_count - oldcapacity) return max_count; geometric = oldcapacity + oldcapacity; if (geometric < newsize) return newsize; return geometric;` |
| `_Get_size_of_n` pre-multiply overflow guard (`_Count > SIZE_MAX / _Ty_size`) | `vector_allocation_bytes(count, &bytes)`: refuses `count > STDLIB_ALLOC_LIMIT / sizeof(int)`, then multiplies. Used by `reserve`, `copy_from`, `shrink_to_fit`. |
| `_Oldsize == max_size()` → `_Xlength()` | explicit `used >= max_count` refusal in `push_back`/`insert` before growth (prevents a saturated, no-op reserve from writing past the buffer). |
| `_Allocation_guard` / `_Reallocation_guard2` commit-last discipline | every fallible step (validate, allocate, copy) precedes a single commit block that releases the old buffer and publishes `items/space/generation`; marked with a `---- commit point` comment in `reserve`. |

Deliberately **not** copied (documented divergence, not an omission):

- **The 1.5x multiplier** (`_Oldcapacity + _Oldcapacity / 2`) was **not** copied.
  C+ keeps its documented 2x step (`oldcapacity + oldcapacity`) and min-4
  seed. Reason: the committed fixtures `deep_error_vector.cp` V1 pin capacity
  4→8→16 and `deep_vector_task4.cp` pins exact capacities, and the in-repo
  mapping `docs/stdlib/container_safety/UPSTREAM_REUSE.md` §4.4 already records
  this as a deliberate non-adaptation. The guard, floor and saturation shape
  *is* ported; only the growth constant differs.
- **The C+ conservative half-limit refusal** (`space > max_count / 2`) is kept
  as an explicit policy in `push_back`/`insert`. It is stricter than upstream
  (which grows to `max_size()`) and is pinned by `deep_vector_task4.cp`
  `T4_PUSH_GROWTH_OVERFLOW` (limit 4096 → 513-int refusal, zero allocation
  delta). It also means the saturation branch of the helper is normally
  reached only through the direct helper checks in the new test.
- Not ported: C++ templates, `if constexpr`, exceptions/`bad_array_new_length`,
  `allocator_traits::max_size`, RAII destructor guards, ASan container
  annotations, `_Vector_val` tombstones, iterator proxy/orphan machinery.
  C+ has none of these; failure is `0`/`-1` plus `stdlib_note`.

**Honesty statement.** No upstream source text was pasted into C++ code. The
upstream shape appears only as a short quote-shaped comment documenting the
ported predicate. This is a concept/structure adaptation with an explicit,
documented multiplier divergence — it is **not** a verbatim copy.

## 4. Note on the strict-build header initialisation

`c+ build --strict --sanitize=none` rejected `size_t bytes;` in three places
with `[CPLUS-LIFETIME] variable 'bytes' is uninitialized (read before it is
initialized)` at `vector.hp:180,317,350`, because the strict analyser does not
infer that `vector_allocation_bytes(count, &bytes)` writes `*out_bytes` on
success. Three minimal `= 0` initialisers were added in `reserve`, `copy_from`
and `shrink_to_fit`. They are required to compile the ported header in strict
mode; they are unrelated to the test-side modulo fix below.

## 5. Regression test design (`vector_stl.cp`)

`tests/stdlib/upstream_reuse/vector_stl.cp` is deterministic and bounded:

- Fixed seed `VST_SEED = 0x5EED` LCG (`state = state*1103515245+12345`, unsigned
  so no signed overflow), `VST_OPS = 2048` steps, model capped at
  `VST_MODEL_MAX = 512` ints. No clock, Random device, environment or asset.
- Operations: `push_back`, `insert`, `erase`, `pop_back`; a one-based reference
  model verifies `size()`, capacity monotonicity and every element via
  `at_checked` after each step.
- Targeted checks: `_Get_size_of_n` exact/overflow/zero; `_Calculate_growth`
  seed/geometric/demand-floor/half-boundary/saturation/at-ceiling; public
  capacity sequence 4→8→16; `reserve(SIZE_MAX)` and `reserve(max_count+1)`
  failure atomicity; injected allocation failure at growth (commit-last:
  state, generation and slice freshness unchanged, one allocation attempt);
  retry publishes once and invalidates the slice.
- Strict-mode fix: the first version used `state % model_len`; `--strict`
  emitted `[CPLUS-ARITH] remainder by zero cannot be excluded`. The final test
  uses a **unique-named bitmask plus clamp** (`idx = (state >> 24) & 0x1ffULL;
  if (idx > model_len) idx = model_len;` / `if (idx >= model_len) idx = 0;`)
  and a one-based model array so no subtraction on `model_len` occurs.

## 6. Exact build / run commands and results

Working directory: `/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib`.

### Strict, no sanitizer (pass)

```
timeout 30 ./build/c+ build tests/stdlib/upstream_reuse/vector_stl.cp --strict --sanitize=none -o build/upstream_reuse/vector_stl_strict
```
`BUILD_RC=0`, empty build log.

```
timeout 30 ./build/upstream_reuse/vector_stl_strict
```
`RUN_RC=0`, empty stderr, stdout:

```
VST_SIZE_OF_N_EXACT 1
VST_SIZE_OF_N_OVERFLOW 1
VST_SIZE_OF_N_ZERO 1
VST_GROWTH_SEED 1
VST_GROWTH_GEOMETRIC 1
VST_GROWTH_DEMAND_FLOOR 1
VST_GROWTH_HALF_BOUNDARY 1
VST_GROWTH_SATURATION 1
VST_GROWTH_AT_CEILING 1
VST_CAPACITY_SEQUENCE 1
VST_RESERVE_OVERFLOW_ATOMIC 1
VST_COMMIT_LAST_FAILED_ATOMIC 1
VST_COMMIT_LAST_RETRY_PUBLISHES 1
VST_DETERMINISTIC_MODEL 1
VECTOR_STL 14 0
```

### UBSan / integer sanitizer (blocked by host toolchain, NOT by this change)

```
timeout 30 ./build/c+ build tests/stdlib/upstream_reuse/vector_stl.cp --strict --sanitize=undefined -o build/upstream_reuse/vector_stl_ubsan
```
`RC=1`, exact output:

```
/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/cplus_runtime.c:130:23: error: use of undeclared identifier 'CPLUS_EXIT_ARITHMETIC'
  130 |     cplus_print_fatal(CPLUS_EXIT_ARITHMETIC, "signed integer overflow", NULL, 0,
      |                       ^
/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/cplus_runtime.c:137:21: error: use of undeclared identifier 'CPLUS_EXIT_ARITHMETIC'
  137 |   cplus_print_fatal(CPLUS_EXIT_ARITHMETIC, kind, NULL, 0, msg);
      |                     ^
2 errors generated.
error: failed to compile instrumented C+ runtime
```

`--sanitize=integer` fails with the identical runtime error (`RC=1`).
`./build/c+ --print-sanitizers` lists `undefined` and `integer` as supported
and backend `/usr/bin/clang`, but the instrumented runtime cannot compile on
this host. A minimal 1-line `int main(void){return 0;}` program built with
`--strict --sanitize=undefined` fails with the **same** two errors, confirming
the failure is global and independent of `vector.hp` and this test. No
UBSan result is claimed.

### Existing committed vector fixtures re-checked (no regression)

```
timeout 30 ./build/c+ build tests/stdlib/deep_error_vector.cp --strict --sanitize=none -o build/upstream_reuse/deep_error_vector_check   # BUILD=0, RUN=0
... last line: DEEP_ERROR_VECTOR 1 F   (matches tests/stdlib/deep_error_vector.expected byte-for-byte)

timeout 30 ./build/c+ build tests/stdlib/deep_vector_task4.cp --strict --sanitize=none -o build/upstream_reuse/deep_vector_task4_check   # BUILD=0, RUN=0
... last line: VECTOR_TASK4 16 0
```

`deep_cross_family.cp` was also attempted but cannot build on this host because
`stdlib/json.hp:20` includes `yyjson.h`, which is not present; this is a
pre-existing dependency gap unrelated to the vector port.

## 7. Limits of this evidence

- A passing strict/no-sanitizer run is evidence of contract conformance for the
  bounded inputs above, not a proof of memory safety. ASan/UBSan could not be
  run (§6).
- The helper functions are unit-tested directly with near-ceiling values
  without allocating (the saturation branch is structurally unreachable from
  `push_back` because of the retained half-limit policy, §3).
- Capacity sequence and exact-capacity behaviour are deliberately preserved;
  a future change to the 1.5x multiplier would require updating the committed
  fixtures named in §3.
