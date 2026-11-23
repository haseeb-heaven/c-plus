# Microsoft STL string techniques → C+ `stdlib/string.hp`

## Provenance (fetched, not recalled)

| Item | Exact value |
| --- | --- |
| Upstream repository | `microsoft/STL` |
| Commit (HEAD at fetch time, `git ls-remote https://github.com/microsoft/STL.git HEAD`) | `0cabaf3c7e0a73fe3040aabce40cd8833e80392b` |
| Capacity/append source | `https://raw.githubusercontent.com/microsoft/STL/main/stl/inc/xstring` |
| Supporting allocation source | `https://raw.githubusercontent.com/microsoft/STL/main/stl/inc/xmemory` |
| Path/append caller | `https://raw.githubusercontent.com/microsoft/STL/main/stl/inc/filesystem` |
| Licence source | `https://raw.githubusercontent.com/microsoft/STL/main/LICENSE.txt` |
| Licence | Apache License 2.0 **WITH LLVM-exception** (`SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception` in every fetched header; exception text present at the end of `LICENSE.txt`) |

The licence permits redistribution of modified source with attribution; the
adapted snippets in `stdlib/string.hp` carry the SPDX-style attribution comment
and this file records the exact upstream URLs and commit.

## What was inspected

In the fetched `xstring` at that commit:

- `_Calculate_growth` (~line 2942): mask the request with `_Alloc_mask`, refuse
  when the mask overflows `max_size()`, guard geometric overflow with
  `_Old > _Max - _Old / 2`, return `max(_Masked, _Old + _Old / 2)`.
- `_Reallocate_for` / `_Reallocate_grow_by` (~lines 2960–3022): allocate the new
  buffer **first**, prepare it in the `_Fn(new, old, old_size, ...)` callback
  while the old buffer is still live, then release the old buffer. Failure
  before the swap leaves the string untouched.
- `_Append` (~lines 3046–3072): if the bytes fit, copy in place with
  `_Traits_move_batch` (`traits::move`, i.e. `memmove` for `char`); otherwise
  delegate to `_Reallocate_grow_by`, copying old prefix and appended bytes
  into the new buffer from the still-live old pointers.
- `_Insert` / `_Replace` (~lines 3100–3220): explicit overlap range splitting
  with pointer comparisons before moving the suffix; constant-evaluated paths
  always reallocate because comparing pointers into unrelated objects is not
  portable.
- `_String_constructor_concat_tag` (~lines 1108–1165): capacity reuse between
  two operands (`_Fits_in_left` / `_Fits_in_right`) for concatenation.

In `filesystem`, `path::operator/=` appends through `_Text.append(...)` / `_Text +=`
i.e. the same `basic_string` append core, and explicitly calls `_Orphan_all()`
before `_Text += _Added` so any alias/iterator invalidation is accounted for.

`_Alloc_temporary2` was searched for in the fetched `xstring`, `xmemory` and
`filesystem` at this commit and **does not occur there**; the alias-safe
technique actually used by these headers is the `_Traits_move_batch` move plus
the allocate-before-retire ordering of `_Reallocate_grow_by`. That is the
pattern this port implements (no claim is made about a helper that is absent
from the fetched sources).

## Changes made to `stdlib/string.hp`

1. Added `stdlib_string_calculate_growth(requested, old_space)` — a direct
   structural port of `_Calculate_growth` (roundup | geometric step | overflow
   guard | hard-limit clamp). Adaptations, all documented in the header:
   - `space` includes the terminating NUL and `STDLIB_ALLOC_LIMIT` replaces
     `max_size()`;
   - the geometric factor stays x2 (the documented C+ capacity contract,
     exercised by many existing tests) instead of MSVC's x1.5;
   - `_Alloc_mask` becomes `STDLIB_STRING_ALLOC_MASK == 0` because
     `stdlib_alloc` has no 16-byte granularity;
   - on geometric overflow MSVC settles at `_Max`; here it settles at the exact
     request, the largest allocation that can still succeed under the checked
     limit (keeps `overflow_string.cp` / `adv_integer_bounds.cp` behaviour);
   - only a constant divisor and bitwise OR are used, so the strict checker's
     "variable division" rule is not triggered.
2. Rewrote `string::append(const char *)`:
   - fit case: `__builtin_memmove` in place, mirroring `_Traits_move_batch`;
     overlap-safe for self-append, interior suffixes and alias-after-`clear()`;
   - growth case: refuse via the growth helper, `stdlib_alloc` the new buffer,
     copy the old prefix, copy the appended bytes from the **still-live** old
     buffer (so `text` may alias it), write the terminator, then release the
     old buffer — the `_Reallocate_grow_by` ordering. Failure-atomic, one
     allocation, `STDLIB_TM_GROWTH` telemetry preserved.
   - This removes the previous manual `uintptr_t` alias-window calculation and
     its dependence on the invariant that an interior source past `count`
     never needs growth.
3. Public API and ABI unchanged: same three fields (`char *`, `size_t`, `size_t`),
   same method names/signatures/return codes, same notes and telemetry.

## Verification (all commands run from the repo root, `timeout 30`)

Canonical strict/no-sanitizer first:

```
./build/c+ build --strict --sanitize=none tests/stdlib/upstream_reuse/string_stl.cp \
  -o $TMP/string_stl_strict
# BUILD_RC=0
$TMP/string_stl_strict
# RUN_RC=0, stderr 0 bytes
# upstream-string-checks 28
# upstream-string-failures 0
```

UBSan, canonical mode attempted second and **blocked by an external toolchain
issue** (not by this change):

```
./build/c+ build --strict --sanitize=undefined ... -o $TMP/string_stl_ubsan
# UBSAN_BUILD_RC=1: /Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/cplus_runtime.c
# error: use of undeclared identifier 'CPLUS_EXIT_ARITHMETIC'
# (external core/runtime header mismatch; documented in other suite runners)
```

Documented workaround (keep the runtime unsanitized, pass Clang UBSan flags to
the lowered headers) succeeded:

```
./build/c+ build --strict --sanitize=none tests/stdlib/upstream_reuse/string_stl.cp \
  -o $TMP/string_stl_ubsan_alt -- -fsanitize=undefined -fno-sanitize-recover=all
# ALT_BUILD_RC=0
$TMP/string_stl_ubsan_alt
# UBSAN_RUN_RC=0, stderr 0 bytes, stdout byte-identical to strict (diff RC=0)
# upstream-string-checks 28 / upstream-string-failures 0
```

Existing capacity/append regressions re-run under strict/no-sanitizer:

```
deep_string_append   rc=0, append-failures 0  (81 checks; forked fatal probes write stderr)
deep_string_task4    rc=0, STRING_TASK4 86 0
allocator/overflow_string rc=0, OVERFLOW_STRING_FAILURES 0
allocation_failure/af_string  rc=0, ALLOC_FAIL 26 0
deep_string_view              rc=0, view-failures 0
regressions/adv_integer_bounds, regressions/adv_lifecycle,
allocator/overflow_realloc, regressions/adv_exact_fit:
  BUILD-FAIL — pre-existing/unrelated strict error in stdlib/vector.hp:335
  [CPLUS-LIFETIME] 'bytes' uninitialized (file owned by another agent, not touched here).
```

## Test coverage

`tests/stdlib/upstream_reuse/string_stl.cp` (28 checks): self-append across
reallocation, repeated growth to 64 bytes, interior-suffix append with and
without reallocation, alias-after-`clear()` (including terminator append),
hard-limit growth refusal with zero allocations and unchanged state,
near-limit exact-growth fallback, and injected allocation-failure atomicity.
It is self-verifying (no `.expected` fixture) and exits non-zero on failure.

## Limitations

- The C+ allocator has no ASan container annotations, so only UBSan evidence
  was available; UBSan does not detect all `memcpy` overlap classes, but the
  implementation uses `memmove` on every potentially overlapping path.
- `max_size()`-style refusal is expressed through `STDLIB_ALLOC_LIMIT` and the
  existing -1 + bounds-note contract rather than a C++ exception.
- Canonical `--sanitize=undefined` remains blocked by the external runtime
  header mismatch; the alternate flags instrument the lowered headers only.
