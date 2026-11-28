# Checked `_s` runtime safety evidence

This record documents **runtime** safety evidence for the checked `_s`
implementations exported by `build/safe/libcplus_safecrt.a`. The evidence is
the executable unit test:

- `tests/crt/unit/test_checked_runtime_safety.c`

Its output contract is a single summary line on stdout,

```
checked_runtime: <checks> <failures>
```

with a non-zero exit status iff `failures > 0`; individual failing checks are
printed to stderr with the check name (and expected/actual values for
`CHECK_EQ`). The final verified result is **`checked_runtime: 96 0`**.

## 1. What is actually under test

All symbols used by the test were verified to be **defined in the archive**
(not merely declared in a header):

```
nm -g --defined-only build/safe/libcplus_safecrt.a
```

The test object's undefined references were extracted and every checked
symbol was confirmed present:

```
nm -u /tmp/checked_test.o | awk '{print $1}' \
  | grep -E '^_(cplus|malloc_s|calloc_s|free_s|free_checked_s)' | sort -u
```

Result — 22/22 references resolved by the archive (all `OK`):

```
_calloc_s                 _cplus_memcmp_s           _cplus_memcpy_s
_cplus_memmove_s          _cplus_memset_s           _cplus_safe_alloc_fail_after
_cplus_safe_alloc_fail_reset  _cplus_safe_log_set_level  _cplus_safe_ptr_size
_cplus_safe_set_constraint_mode  _cplus_sc__memcpy_s_chk
_cplus_sc__strcat_s_chk   _cplus_sc__strcpy_s_chk   _cplus_sc_set_mem_constraint_handler_s
_cplus_sc_set_str_constraint_handler_s  _cplus_strcat_s  _cplus_strcpy_s
_cplus_strncat_s          _cplus_strncpy_s          _free_checked_s
_free_s                   _malloc_s
```

Three subject groups:

1. **Literal safeclib "checked" entry points** (`*_chk`): the wrappers in the
   archive call these with `BOS_UNKNOWN`; the test calls them **directly with
   explicit object-size bounds** so the `CHK_*` / `*_OVR_CLEAR` paths are
   exercised. Declared locally in the test because the archive ships no
   public header for them.
2. **C+ checked adapters** (`cplus_*_s` from `stdlib/safe/{safe_memory,
   safe_string}.h`): safeclib-backed constraint-checking `_s` API.
3. **Checked allocator** (`stdlib/safe/safe_alloc.h`): `malloc_s`, `calloc_s`,
   `free_s`, `free_checked_s`, plus `cplus_safe_alloc_fail_after` /
   `cplus_safe_alloc_fail_reset` fault injection.

Constraint handlers are installed after
`cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE)` because the mode
setter resets handlers to the verbose ignore handler; the quiet handler keeps
stderr limited to genuine failures.

Error codes are the safeclib constraint codes (positive, **not** platform
`errno`): `ESNULLP=400`, `ESZEROL=401`, `ESLEMAX=403`, `ESOVRLP=404`,
`ESNOSPC=406`. Test with `!= 0`, never `< 0`.

## 2. Coverage matrix

Each row is a runtime observation made by the test (96 `CHECK`/`CHECK_EQ`
assertions in total).

| Required scenario | API(s) | Observed behavior |
|---|---|---|
| null dest | `_chk` memcpy/strcpy/strcat, adapter memcpy/memset/strcpy/strcat | refused, `400` |
| null src | `_chk` memcpy, adapter memcpy/strcpy/strcat | refused, `400` |
| null out-param | adapter `memcmp_s(diff=NULL)` | refused, `400` |
| zero length | `_chk` memcpy (`dmax=0`) / adapter memcpy (`dmax=0`) | `401` |
| zero length (no-op) | adapter memcpy (`smax=0`), `memset_s(dmax=0,n=0)` | defined no-op, `0` |
| destination exactly fit | memcpy (4/4), strcpy (dmax=4 for `"abc"`), strcat (dmax=6), memset (4/4), strncpy/strncat truncation | accepted, `0`, content verified |
| one-byte-too-small | memcpy (3/4), strcpy (3 vs `"abc"`), strcat (5 vs `"xy"+"abc"`), `_chk` variants | refused, `406` |
| `SIZE_MAX` n | adapter memcpy (`smax`/`dmax`), memset, strncpy (`slen`), `_chk` memcpy | refused, `403` |
| overlapping `memcpy_s` | `_chk` and adapter `memcpy_s(d+1,8,d,8)` | refused, `404`; `memmove_s` overlap succeeds with correct bytes |
| unterminated source | `strcpy_s`/`strcat_s` with an 8-byte all-`'Z'` source and `dmax=8` | refused (non-zero, `406`); no read beyond the 8-byte source object |
| bounded truncation | `strncpy_s(...,slen=2)` → `"ab"`; `strncat_s(...,slen=2)` → `"xyab"`; `slen=0` → `""` | accepted, `0`, NUL-terminated, guards intact |
| failure atomicity | canary `0xCC` beyond `dmax` after every refusal above | canary verified intact in each case; first `dmax` bytes are defensively cleared |
| allocation overflow | `malloc_s(SIZE_MAX)`, `calloc_s(SIZE_MAX,2)` | `NULL` + non-zero `errno` |
| zero-size allocation | `calloc_s(2,0)` | defined tracked 1-byte zeroed block |
| failure injection | `fail_after(1)` → 1st alloc OK, 2nd `NULL`/`ENOMEM`; `fail_reset()` → OK | observed as documented |
| double-free / invalid-free refusal | `free_checked_s(p)` twice, then `free_checked_s(&stack_obj)` | first `0`, second `EINVAL`, stack `EINVAL`; `free_checked_s(NULL)` → `0` |

### Documented quirk found during verification

`cplus_memset_s(d, 0, 0, 1)` returns `406` (not `401`): the safeclib
implementation checks `n > dmax` before `dmax == 0`. With `n == 0` the call is
a defined no-op returning `0`. The test asserts the **observed** behavior and
the table above records the distinction; the generic
"zero length → `401`" rule in `safe_memory.h` is therefore not universal.

## 3. Exact verification commands and results

All commands run from the repository root on branch `agent/modern-stdlib`.
Compiler: Apple clang 16 (`/usr/bin/clang`, `clang-1600.0.26.6`), target
`arm64-apple-darwin27.2.0`. `libcplus_safecrt.a` was **pre-existing** (the test
does not rebuild it).

### 3.1 Strict C99 compile — no warnings

```
/usr/bin/clang -std=c99 -Wall -Wextra -pedantic -Istdlib/safe \
  tests/crt/unit/test_checked_runtime_safety.c \
  build/safe/libcplus_safecrt.a -lm -pthread -o /tmp/checked_runtime_normal
```

Output: **empty** (no diagnostics). Exit: 0.

### 3.2 Normal run

```
/tmp/checked_runtime_normal
checked_runtime: 96 0
echo $?   # 0
```

### 3.3 UBSan (-O0), halt on error

```
/usr/bin/clang -std=c99 -Wall -Wextra -pedantic \
  -fsanitize=undefined -fno-sanitize-recover=all -Istdlib/safe \
  tests/crt/unit/test_checked_runtime_safety.c \
  build/safe/libcplus_safecrt.a -lm -pthread -o /tmp/checked_runtime_ubsan
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 /tmp/checked_runtime_ubsan
```

Output:

```
checked_runtime: 96 0
```

Exit: 0. No UBSan reports.

### 3.4 Optimized (-O2) strict run

```
/usr/bin/clang -std=c99 -O2 -Wall -Wextra -pedantic -Istdlib/safe \
  tests/crt/unit/test_checked_runtime_safety.c \
  build/safe/libcplus_safecrt.a -lm -pthread -o /tmp/checked_runtime_o2
/tmp/checked_runtime_o2
```

Output:

```
checked_runtime: 96 0
```

Exit: 0. No diagnostics.

### 3.5 Optimized + UBSan

```
/usr/bin/clang -std=c99 -O2 -Wall -Wextra -pedantic \
  -fsanitize=undefined -fno-sanitize-recover=all -Istdlib/safe \
  tests/crt/unit/test_checked_runtime_safety.c \
  build/safe/libcplus_safecrt.a -lm -pthread -o /tmp/checked_runtime_o2_ubsan
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 /tmp/checked_runtime_o2_ubsan
```

Output:

```
checked_runtime: 96 0
```

Exit: 0. No UBSan reports.

### 3.6 Repository test-harness flag compatibility

Compiled with the exact flag set from `tests/crt/unit/run_unit_tests.sh`
using `/usr/bin/clang` (root `-I`, `stdlib/safe`, both third-party include
dirs, `-O2 -Wall -Wextra -pedantic -std=c99`): build clean, run
`checked_runtime: 96 0`, exit 0.

### 3.7 Negative control (harness is not vacuous)

During development an earlier revision of the test (before restoring a
canary and before correcting the `memset_s` zero-length expectation) produced:

```
FAIL: adapter memcpy exact fit guards intact
FAIL: adapter memset zero dmax refused (401) (got 406, expected 401)
checked_runtime: 94 2
exit=1
```

This demonstrates that `CHECK`/`CHECK_EQ` count failures, print diagnostics,
and drive a non-zero exit status; the passing `96 0` result is therefore not a
vacuous pass. The two assertions were corrected to match observed library
behavior (canary restored after a rejected call; `406` for `dmax=0,n=1`), not
removed.

## 4. Known limitations — read this before citing the result

1. **Runtime checks are not compile-time safety.** Passing this test proves
   only that the named checks fired for the named inputs at runtime. It does
   **not** prove compile-time safety, memory-safety by construction, pointer
   provenance, ownership, lifetime, or alias/UAF safety. The checked `_s`
   functions cannot see dangling pointers that still look live, and a caller
   can still pass a wrong-but-in-range `dmax`.
2. **Canaries are byte sentinels beyond `dmax`, not whole-object proofs.** The
   "atomicity" observations show the first `dmax` bytes are defensively cleared
   and bytes beyond `dmax` were unchanged *for these inputs*. Safeclib's
   clearing behavior is implementation-specific, not an ISO C11 K.3 guarantee.
3. **The archive was not rebuilt with sanitizers.** UBSan instrumented the
   test translation unit only; the prebuilt `libcplus_safecrt.a` objects were
   not recompiled with `-fsanitize=undefined`. ASan was not run. The absence
   of UBSan reports is meaningful but not exhaustive coverage of the library
   internals.
4. **Unterminated-source checks are deliberately bounded.** The all-`'Z'`
   source is exactly 8 bytes and `dmax=8`, so the checked scan cannot read past
   the source object. Larger `dmax` on an unterminated source would be an
   unbounded read and is intentionally not tested.
5. **Allocator detection is table-based and bounded.** Double/invalid-free
   refusal relies on a bounded recent-free cache and a live-allocation table;
   a very old freed address whose storage was reused is not distinguishable as
   a double free. `free_checked_s` refusing a raw `malloc()`/stack pointer is
   evidence of *this* table's coverage, not a general heap-safety guarantee.
6. **Constraint-code values are environment-specific.** The codes `400/401/
   403/404/406` come from the pinned safeclib headers in this build. `errno`
   values are platform errno values (`EINVAL`, `ENOMEM`); on macOS
   `calloc_s` overflow reports `EOVERFLOW = 84`, while `CPLUS_SAFE_EOVERFLOW`
   in `safe_error.h` is `75` (Linux). The test checks `errno != 0` for overflow
   and the system `ENOMEM`/`EINVAL` constants for injection/refusal to avoid
   that mismatch.
7. **Toolchain note.** In this environment `clang` on `PATH` is Homebrew LLVM
   23 with a broken sysroot (`no such sysroot directory .../MacOSX27.sdk`,
    then `errno.h: file not found`). Use `/usr/bin/clang` (Apple clang 16) as
   in the commands above, or set `CPLUS_CLANG=/usr/bin/clang` when invoking
   `tests/crt/unit/run_unit_tests.sh`. This is an environment issue, unrelated
   to the test source.
8. **No source changes were made.** Per task ownership, only the test and this
   document were created; no library source, header, or build script was
   modified, and nothing was staged or committed.
