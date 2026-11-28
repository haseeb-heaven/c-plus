# Deterministic allocation-failure matrix

Test: `tests/libc/test_alloc_fault_matrix.c`
Libraries (existing build artifacts; this suite does **not** rebuild them):

- `build/libc/libcplus_hardened.a`
- `build/safe/libcplus_safecrt.a`

## Why the C+ fault-injection API is not used

The task names `stdlib_alloc_fail_at` / `stdlib_alloc_fail_after` /
`stdlib_alloc_fail_reset` from `stdlib/error.hp`. That file is a **C+ (`C+`)
header, not a C header**: it declares `class error` with method bodies, uses
C+ method-call sugar (`error_reset(this)`), and defines a weak `stdlib_state`
object plus `static inline` helpers whose bodies call the C+ allocator
(`cplus_alloc` / `cplus_release` from `cplus/cplus.h`). It cannot be included
from the `-std=c99` translation unit this matrix requires, so none of its
helpers — nor its telemetry state — are reachable from plain C.

The matrix therefore uses the **C-callable hooks owned by the allocation
wrappers themselves**:

| Family | Hook API | Declared in |
| --- | --- | --- |
| Hardened libc | `cplus_alloc_fail_after(long)`, `cplus_alloc_fail_reset(void)` | `libc/install-include/cplus_libc.h` |
| SafeCRT | `cplus_safe_alloc_fail_after(long)`, `cplus_safe_alloc_fail_reset(void)`, `cplus_safe_alloc_seq(void)` | `stdlib/safe/safe_alloc.h` |

Both hooks are **latching** ("N successes, then fail every later attempt until
reset"), whereas `stdlib_alloc_fail_at(n)` means "the n-th attempt fails once".
The observable mapping used by the test, with exact assertions at every step:

| Matrix case | Hook sequence | Asserted outcome |
| --- | --- | --- |
| `fail_at=0` | `fail_reset()` (injection off) | block returned, `errno == 0`, exact metadata size, live count +1 |
| `fail_at=1` | `fail_after(0)` → failing call → `fail_reset()` → next call | first call `NULL`/`ENOMEM`, no block recorded, sequence = 1 (SafeCRT); after reset the next call succeeds with `errno == 0` |
| `fail_after=0` | `fail_after(0)` → two calls → `fail_reset()` → next call | both calls `NULL`/`ENOMEM`, sequence = 2 (SafeCRT); after reset the next call succeeds |

## Wrappers and scenarios covered

Every scenario asserts the exact return value, `errno`, metadata byte size and
live-block count; injected failures additionally assert that the owner of a
`realloc` block is left untouched.

| Wrapper | off / success | one-shot failure | latching failure | zero size | exact fit | `SIZE_MAX` | NULL realloc | double-free refusal | logging parity |
| --- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| `cplus_malloc` | ✓ | ✓ | ✓ | ✓ (1-byte block) | ✓ (37 B) | ✓ `ENOMEM` | — | — | ✓ success + injected |
| `cplus_calloc` | ✓ | ✓ | ✓ | ✓ `(0,8)` and `(8,0)` | ✓ (7×9 = 63 B, zeroed) | ✓ `ENOMEM` | — | — | ✓ success + injected |
| `cplus_realloc` | ✓ grow/shrink | ✓ owner preserved | ✓ via NULL owner | ✓ (1-byte block) | ✓ 32→96→40 B | ✓ `ENOMEM`, owner preserved | ✓ acts as malloc | — | ✓ success + injected |
| `cplus_free` | ✓ | — | — | — | — | — | — | ✓ double-free counter +1 | — |
| `cplus_free_checked` | ✓ handle nulled on success | — | — | — | — | — | — | ✓ stale alias → `EINVAL`, NULL no-op | ✓ success + refusal |
| `malloc_s` | ✓ | ✓ | ✓ | ✓ (1-byte block) | ✓ (37 B) | ✓ `EINVAL` | — | — | ✓ success + injected |
| `calloc_s` | ✓ | ✓ | ✓ | ✓ `(0,8)` and `(8,0)` | ✓ (7×9 = 63 B, zeroed) | ✓ `EOVERFLOW` | — | — | ✓ success + injected |
| `realloc_s` | ✓ grow/shrink | ✓ owner preserved | ✓ via NULL owner | ✓ (1-byte block) | ✓ 32→96→40 B | ✓ `EINVAL`, owner preserved | ✓ acts as malloc | — | ✓ success + injected |
| `free_s` | ✓ | — | — | — | — | — | — | ✓ `cplus_safe_ptr_was_freed()` | — |
| `free_checked_s` | ✓ by-value alias unchanged | — | — | — | — | — | — | ✓ `CPLUS_SAFE_EINVAL` + `EINVAL`, NULL no-op | ✓ success + refusal |
| `aligned_alloc_s` | ✓ alignment honoured | ✓ | ✓ latching + recovery | ✓ (1-byte block) | ✓ (32-byte align, 96 B) | ✓ `EINVAL` | — | — | ✓ injected |

Additional checks:

- `aligned_alloc_s(3, 64)` is refused with `NULL`/`EINVAL` (non-power-of-two
  alignment).
- `realloc(NULL, SIZE_MAX)` and `realloc_s(NULL, SIZE_MAX)` route through the
  malloc-like guard (`ENOMEM` / `EINVAL` respectively).
- `calloc(2, SIZE_MAX)` (product overflow) and `calloc(SIZE_MAX, 2)` (count
  overflow) are both refused without allocation.
- The log toggle itself is verified: with logging **ON**, stderr capture sees
  both `[CPLUS-LIBC]` and `[SAFECRT]` lines; with logging **OFF**, the same calls
  emit nothing. Parity checks run with stderr redirected to `/dev/null` so the
  output stays clean.
- No test intentionally exhausts memory: all `SIZE_MAX` requests are rejected
  by the wrappers' own arithmetic or by the host allocator before any large
  allocation succeeds.

### Notable cross-library differences (asserted, not smoothed over)

- `SIZE_MAX` overflow: hardened `cplus_malloc`/`cplus_calloc`/`cplus_realloc`
  report `ENOMEM`; SafeCRT `malloc_s`/`realloc_s`/`aligned_alloc_s` report
  `EINVAL` and `calloc_s` reports `EOVERFLOW`.
- Checked free signatures differ: hardened `cplus_free_checked(void **)` nulls
  the caller's handle on success and returns `0`/`EINVAL`; SafeCRT
  `free_checked_s(void *)` takes the pointer by value, leaves the alias
  unchanged and returns `CPLUS_SAFE_OK`/`CPLUS_SAFE_EINVAL` with `errno`.
- SafeCRT exposes an attempt counter (`cplus_safe_alloc_seq`); hardened libc
  does not, so sequence assertions are made only where the API exists.

## Verification

From the repository root:

```sh
# Plain C99, strict warnings (existing libs only):
/usr/bin/clang -std=c99 -Wall -Wextra -pedantic \
  -Ilibc/install-include -I. \
  tests/libc/test_alloc_fault_matrix.c \
  build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
  -lm -pthread -o /tmp/alloc_fault_matrix
/tmp/alloc_fault_matrix

# UBSan variant:
/usr/bin/clang -std=c99 -Wall -Wextra -pedantic \
  -fsanitize=undefined -fno-sanitize-recover=undefined \
  -Ilibc/install-include -I. \
  tests/libc/test_alloc_fault_matrix.c \
  build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
  -lm -pthread -o /tmp/alloc_fault_matrix_ubsan
/tmp/alloc_fault_matrix_ubsan
```

Both print exactly:

```
alloc_fault_matrix: 294/294 checks ok
```

Compiler warnings: **none** in either configuration. UBSan diagnostics:
**none**. Exit status: `0` on full pass, `1` if any check fails (each failure
also prints a `FINDING: <operation>: <detail>` line).

## APIs deliberately not covered, with exact reasons

- `stdlib_alloc_fail_at`, `stdlib_alloc_fail_after`, `stdlib_alloc_fail_reset`,
  `stdlib_alloc` / `stdlib_calloc` / `stdlib_release`, `stdlib_state` and the
  `STDLIB_TM_*` telemetry counters — all defined in `stdlib/error.hp`, a C+
  header that cannot be included from a C99 translation unit. Their C-callable
  equivalents (the two libraries' own hooks) are covered instead.
- `cplus_aligned_alloc` — declared in `libc/install-include/cplus_libc.h` and
  present in `libcplus_hardened.a`, but it is a plain host-backed wrapper
  (`libc/src/stdlib/stdlib_c17.c`) with no owned metadata table and no
  allocation-failure hook; `cplus_alloc_fail_after` does not affect it, and its
  zero-size behaviour is host-defined. The task's enumerated set is
  `cplus_malloc`/`cplus_calloc`/`cplus_realloc`/`cplus_free` plus the
  `safe_alloc.h` checked variants, all of which are covered.
- Metadata/generation/ownership-transfer helpers (`cplus_alloc_get_meta`,
  `cplus_safe_ptr_check_generation`, `cplus_transfer_ownership`,
  `cplus_safe_transfer_ownership`, live-byte counters) — not allocation
  wrappers; they are not part of this matrix's scenario set.

## Limitations

- Double-free detection is diagnostic, not proof: the recent-free tables are
  bounded (1024 slots) and a recycled address can be indistinguishable from
  its new owner. The test frees only one block at a time and checks refusal
  immediately, so it exercises the defined path only.
- Logging parity compares observable results (return, `errno`, metadata size,
  attempt sequence) with logging off and on; it does not assert log formatting
  beyond the two prefix markers checked in `test_logging_emission`.
- The "one-shot" `fail_at=1` case is emulated with an explicit `fail_reset()`
  after the injected failure, because the C hooks are latching. This is stated
  at the call site and in the mapping table above.
