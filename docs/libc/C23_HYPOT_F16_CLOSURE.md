# C23 hypot3 / `_Float16` math closure — evidence and result

**Date:** 2026-11-23
**Branch:** `agent/modern-stdlib`
**Scope:** the five `math.h` APIs assigned to this agent: the 3-argument
`hypot` overload and the four `_Float16` helpers `frexp16`, `fma16`, `logb16`,
`exp16`. All five are classified `MISSING` in `docs/libc/iso_c_inventory.json`
(lines 3760–3816) and were declared-only in
`libc/src/math/math_c23.c` / `libc/src/math/math_c23_upstream.c`.

**Outcome: 5 of 5 CLOSED** as additive definitions in
`libc/src/math/math_c23_hypot3_f16.c`. No existing file was modified, no
compiler/runtime was changed, and no LLVM libc `src/__support` C++ internals
were vendored. No algorithm, constant, table or polynomial was fabricated:
every body delegates to a real, probed host primitive. Semantic deviations
are itemised in §5.

---

## 1. Files created (strict ownership honoured)

| File | Purpose |
|---|---|
| `libc/src/math/math_c23_hypot3_f16.c` | Definitions of the five `cplus_*` entry points |
| `libc/install-include/cplus_math_c23_hypot3_f16.h` | Public declarations (C/C++ linkage, `_Float16` guard) |
| `tests/libc/test_math_c23_hypot3_f16.c` | Standalone test harness (530 checks) |
| `docs/libc/C23_HYPOT_F16_CLOSURE.md` | This document |

No other file created or modified. No `git add` / `git commit`. No CMake,
compiler, or runtime edit.

---

## 2. Host probe evidence (exact commands and results)

Toolchain: `/usr/bin/clang --version` →
`Apple clang version 16.0.0 (clang-1600.0.26.6)`, target
`arm64-apple-darwin27.2.0`.

### 2.1 Language / type support

```
$ /usr/bin/clang -std=c17 -dM -E -x c /dev/null | grep -E '__FLT16_MANT_DIG__|__STDC_VERSION__'
#define __FLT16_MANT_DIG__ 11
#define __STDC_VERSION__ 201710L

$ /usr/bin/clang -std=c2x -dM -E -x c /dev/null | grep -E '__FLT16_MANT_DIG__|__STDC_VERSION__'
#define __FLT16_MANT_DIG__ 11
#define __STDC_VERSION__ 202000L
```

`_Float16` **is** available on this host (matches
`C23_MATH_DEPENDENCY_CLOSURE.md` §2 correction; contradicts the stale claim in
`MATH_GAPS_EVIDENCE.md:67,159-161`). Apple Clang 16 has no `-std=c23` spelling;
`-std=c2x` reports `__STDC_VERSION__ == 202000L`.

### 2.2 Builtin availability (`__has_builtin`), `-std=c17` and `-std=c2x` identical

```
$ /usr/bin/clang -std=c17 probe_builtins.c -o probe_builtins17 && ./probe_builtins17
__has_builtin(__builtin_hypot3)   = 0
__has_builtin(__builtin_hypot)    = 1
__has_builtin(__builtin_frexp16)  = 0
__has_builtin(__builtin_fma16)    = 0
__has_builtin(__builtin_logb16)   = 0
__has_builtin(__builtin_exp16)    = 0
__has_builtin(__builtin_frexpf16) = 1
__has_builtin(__builtin_fmaf16)   = 1
__has_builtin(__builtin_f16fma)   = 0
__has_builtin(__builtin_logbf16)  = 0
__has_builtin(__builtin_expf16)   = 1
__has_builtin(__builtin_hypotf16) = 0

$ /usr/bin/clang -std=c17 probe_host.c -o probe_host && ./probe_host
frexp=1 frexpf=1 logb=1 logbf=1 hypot=1 hypotf=1 fma=1 fmaf=1 expf=1
```

**Key discovery the earlier audit missed:** `C23_MATH_DEPENDENCY_CLOSURE.md`
§2 only checked the C23-named builtins (`__builtin_frexp16`, `__builtin_fma16`,
`__builtin_logb16`, `__builtin_exp16`) and concluded "Apple clang 16 exposes
none of these builtins." Clang 16 in fact exposes the LLVM-style f16-suffixed
builtins `__builtin_fmaf16`, `__builtin_expf16` and `__builtin_frexpf16`. Those
are genuine compiler primitives, not upstream C++ internals, and are what make
the closure possible here.

### 2.3 3-argument `hypot` syntax is rejected by the host header

```
$ cat probe_hypot3.c
#include <math.h>
int main(void){ double r = hypot(3.0,4.0,12.0); return r > 0 ? 0 : 1; }

$ /usr/bin/clang -std=c17 -fsyntax-only probe_hypot3.c   # and -std=c2x (identical)
error: too many arguments to function call, expected 2, have 3
```

No `__builtin_hypot3` exists. Upstream LLVM libc has only 2-arg `hypot`
(`generic/hypot.cpp` → `fputil::hypot`; `generic/hypot3.cpp` is HTTP 404 at the
pin `424c2d9…`, per `C23_MATH_DEPENDENCY_CLOSURE.md` §3 row 19).

### 2.4 The `__builtin_frexpf16` backend crash (why frexp16 uses a composition)

```
$ /usr/bin/clang -std=c17 -O0 -DCASE_frexpf16 probe_one_frexpf16.c -o ...
fatal error: error in backend: Cannot select: ... i32 = any_extend ...
clang: error: clang frontend command failed with exit code 70
EXIT=74

$ /usr/bin/clang -std=c17 -O1 -DCASE_frexpf16 ...   # EXIT=0
$ /usr/bin/clang -std=c17 -O2 -DCASE_frexpf16 ...   # EXIT=0
$ /usr/bin/clang -std=c17 -O3 -DCASE_frexpf16 ...   # EXIT=0
$ ./probe_f16_o2
frexpf16: r=0.500000 e=0
```

The native f16 `frexp` builtin is unusable under the default `-O0` build, so
`cplus_frexp16` deliberately uses the exactly-equivalent `frexpf` + conversion
path (§4.2), which works at every `-O` level.

### 2.5 Builtin lowering / external symbols

```
$ nm -u probe_one_fmaf16   → _printf              (no fmaf16/libcall symbol)
$ nm -u probe_one_expf16   → _expf, _printf       (lowers via standard expf)
$ nm -u probe_f16_o2       → _printf              (frexpf16 path lowerable inline)
```

`expf` is a standard C99 libm symbol present in macOS libSystem, so no
f16-specific external dependency is introduced.

### 2.6 Numerical differential validation of the host primitives

```
$ /usr/bin/clang -std=c17 probe_diff.c -o probe_diff && ./probe_diff
differential: n=404 exp_mismatch=0 fma_mismatch=0

$ /usr/bin/clang -std=c17 -include float.h probe_comp.c -o probe_comp -lm && ./probe_comp
composition diff: frexp mism=0 logb mism=0 hypot3(>4ulp) mism=0  (n=61)
hypot3(DBL_MAX,DBL_MAX,DBL_MAX)=inf (expect inf)
hypot3(3,4,12)=13 (expect 13)
```

- `__builtin_expf16` matched `(_Float16)exp((double)x)` at all 404 points.
- `__builtin_fmaf16` matched a double-`fma` reference at all 404 points.
- `frexpf`/`logbf` compositions matched double references at all 61 points.
- Nested `hypot` matched a long-double reference within 4 ulp, stayed
  overflow-safe, and was exact for integer triples.

---

## 3. Integration rule applied

The rule in `C23_MATH_DEPENDENCY_CLOSURE.md` §2 forbids copying bodies whose
closure needs LLVM libc C++ internals (`src/__support/...`) and permits
dependency solely on libc functions this project/host already provides.
No upstream `src/math/generic/*.cpp` file was copied. Instead each API is a
thin delegation to a host primitive:

| API | Host primitive(s) | Dependency-closed? | Upstream C++ internals used? |
|---|---|---|---|
| `hypot3` | `hypot` (twice) | yes (`<math.h>`) | no |
| `frexp16` | `frexpf` + exact f32↔f16 conversions | yes | no |
| `fma16` | `__builtin_fmaf16`, fallback `fma` (double) | yes | no |
| `logb16` | `logbf` + exact conversions | yes | no |
| `exp16` | `__builtin_expf16`, fallback `exp` (double) | yes (`expf` in libSystem) | no |

---

## 4. Implementation per API

### 4.1 `cplus_hypot3(double,double,double)`
`return hypot(hypot(x, y), z);` — the host's own overflow-safe 2-arg `hypot`
nested. Avoids the catastrophic intermediate overflow of a hand-written
`sqrt(x*x+y*y+z*z)`; the earlier audit's "no fabricated algorithm" warning was
specifically about that naive `sqrt` form, which is **not** used here.

### 4.2 `cplus_frexp16(_Float16,int*)`
`return (_Float16)frexpf((float)x, exp);` — f16→f32 is exact; the f16
significand fits in 11 bits so the f32→f16 reverse conversion is exact; the
`int` exponent is correct across the f16 range. The `__builtin_frexpf16`
builtin was rejected because of the `-O0` backend crash (§2.4).

### 4.3 `cplus_fma16(_Float16,_Float16,_Float16)`
`__builtin_fmaf16(x,y,z)` when present, else
`(_Float16)fma((double)x,(double)y,(double)z)`. The fallback is correctly
rounded because a fused f16 multiply-add is exactly representable in double
(22-bit product + exponent spread ≤ 53 bits).

### 4.4 `cplus_logb16(_Float16)`
`return (_Float16)logbf((float)x);` — `floor(log2|x|)` is an integer in
`[-24,15]` for every finite f16 value, exactly representable in f16; zero /
inf / NaN classifications propagate correctly.

### 4.5 `cplus_exp16(_Float16)`
`__builtin_expf16(x)` when present, else `(_Float16)exp((double)x)`.

---

## 5. Semantic deviations (explicit)

1. **`hypot3` double rounding.** Two nested `hypot` calls round twice; the
   result can differ by ~1 ulp from a single-step correctly-rounded 3-arg
   hypot. IEEE 754 does not mandate a hypot3, and the implementation is
   overflow-safe. This is the only accuracy deviation.
2. **`exp16` lowering via `expf`.** On this host `__builtin_expf16` lowers
   through the standard float `expf` plus conversions. Differential testing
   (§2.6) found no mismatch over 404 points; a future host with a less
   accurate `expf` could in principle double-round, hence documented.
3. **`frexp16` / `logb16` use float-precision host primitives** rather than a
   native f16 builtin. For `_Float16` inputs these are provably exact
   (§4.2, §4.4), so there is no numerical deviation; the only consequence is a
   reliance on the standard `frexpf`/`logbf` symbols.
4. **No deviation for `fma16`** at the probed domain; the double fallback is
   correctly rounded.

No fabricated values: there are no hand-written constants, lookup tables,
polynomials, bit hacks or rounding emulation in the implementation.

---

## 6. Verification (exact commands and results)

Test build/run — passes under `-std=c17` and `-std=c2x`, `-O0` and `-O2`,
and with `-Wpedantic` (all with `-Wall -Wextra`, `-Ilibc -Ilibc/install-include`):

```
$ /usr/bin/clang -std=c17 -Wall -Wextra -Ilibc -Ilibc/install-include \
    libc/src/math/math_c23_hypot3_f16.c libc/log/log.c \
    tests/libc/test_math_c23_hypot3_f16.c -lm -o /tmp/test_math_c23_hypot3_f16
$ /tmp/test_math_c23_hypot3_f16
test_math_c23_hypot3_f16: checks=530 failed=0
test_math_c23_hypot3_f16: ok
```

Matrix (all compile EXIT=0, run EXIT=0, 530 checks / 0 failed):
`-std=c17 -O0`, `-std=c17 -O2`, `-std=c2x -O0`, `-std=c2x -O2`,
`-std=c17 -O0 -Wpedantic`.

ABI / additive check:

```
$ /usr/bin/clang -std=c17 -Ilibc -Ilibc/install-include \
    -c libc/src/math/math_c23_hypot3_f16.c -o /tmp/h3f16.o
$ nm -g /tmp/h3f16.o | grep ' T '
000000000000011c T _cplus_exp16
0000000000000094 T _cplus_fma16
000000000000004c T _cplus_frexp16
0000000000000000 T _cplus_hypot3
00000000000000dc T _cplus_logb16
```

Linking the new source together with the existing declaration-only
`math_c23.c` and `math_c23_upstream.c` succeeds with **no duplicate-definition
errors**, confirming the additive ABI preservation (no existing symbol
changed; previously-undefined symbols now resolve).

---

## 7. Residual (non-blocking) upstream gaps — for completeness

These do **not** block the closures above; they explain why compositions were
chosen and what a "single-step native" future closure would require:

| API | Native single-step option | Status at pin `424c2d9…` / this host |
|---|---|---|
| `hypot3` | `__builtin_hypot3` / upstream 3-arg source | absent on host; `generic/hypot3.cpp` HTTP 404 upstream |
| `frexp16` | `__builtin_frexpf16` | present but crashes the arm64 backend at `-O0` |
| `fma16` | `__builtin_fmaf16` | present and correct (used) |
| `logb16` | `__builtin_logbf16` | absent on host (`has_builtin=0`) |
| `exp16` | `__builtin_expf16` | present and correct (used) |

The prior blanket conclusion in `C23_MATH_DEPENDENCY_CLOSURE.md` that all five
are unclosable was based on checking only the C23-named builtins and on the
absence of vendored C++ upstream; the genuine host primitives above invalidate
that conclusion for this host. This document supersedes rows 19–23 of that
document's §3 table for this branch, without editing it (strict file
ownership).

---

## 8. Build integration (integration agent, 2026-11-23)

The five hypot3/_Float16 entry points (and the eight farith entry points) were
wired into the standard libc build. Files changed (integration scope only):

| File | Change |
|---|---|
| `libc/CMakeLists.txt` | Added `src/math/math_c23_hypot3_f16.c` and `src/math/math_c23_farith.c` to `LIBC_SOURCES`. |
| `tests/libc/build_libc.sh` | Added both translation units to the source loop producing `build/libc/libcplus_hardened.a`. |
| `libc/src/math/math_c23.c` | Removed the now-stale `extern` declarations for `cplus_hypot3` and the four `_Float16` helpers (their signatures matched the new definitions exactly and duplicated them); the branches now point to `math_c23_hypot3_f16.c`. |
| `libc/src/math/math_c23_upstream.c` | Removed the same five externs; the `_Float16` guard remains as an empty, commented block. |
| `libc/install-include/cplus_libc.h` | Additively included `cplus_math_c23_hypot3_f16.h` and `cplus_math_c23_farith.h`. |
| `docs/libc/C23_HYPOT_F16_CLOSURE.md`, `docs/libc/C23_FARITH_CLOSURE.md` | Added this integration record. |

The four `/f` farith externs were deliberately left in place: the new
translation units do not define them, so removing them is outside the
"duplicate/conflict with the new definitions" scope of this task.

### Verification (exact, `CPLUS_CLANG=/usr/bin/clang`)

```
$ CPLUS_CLANG=/usr/bin/clang sh tests/libc/build_libc.sh
build/libc/libcplus_hardened.a

$ nm -g build/libc/libcplus_hardened.a | grep ' T _cplus_'  # excerpt
0000000000000000 T _cplus_hypot3
0000000000000050 T _cplus_frexp16
000000000000009c T _cplus_fma16
00000000000000e0 T _cplus_logb16
000000000000011c T _cplus_exp16

$ CPLUS_CLANG=/usr/bin/clang sh tests/libc/run_libc_tests.sh
libc tests: 90 passed, 0 failed
ok test_math_c23_hypot3_f16.c
ok test_math_c23_farith.c
```

The first suite run showed 4 transient failures in unrelated stdio/coverage
tests while parallel agents rebuilt the shared `build/libc/` archive; a clean
re-run and individual runs of all four passed (90/90). No source error.

Direct link (source + log + test, no archive), exit 0, no warnings:

```
$ /usr/bin/clang -std=c17 -Wall -Wextra -Ilibc -Ilibc/install-include \
    libc/src/math/math_c23_hypot3_f16.c libc/log/log.c \
    tests/libc/test_math_c23_hypot3_f16.c -lm -o /tmp/test_math_c23_hypot3_f16
$ /tmp/test_math_c23_hypot3_f16 | tail -2
test_math_c23_hypot3_f16: checks=530 failed=0
test_math_c23_hypot3_f16: ok
```

CMake source-list validation (isolated build dir, static target):

```
$ cmake -S libc -B <tmp> -DCMAKE_C_COMPILER=/usr/bin/clang
$ cmake --build <tmp> --target cplus_hardened_libc --parallel 4
[ 86%] Building C object CMakeFiles/.../src/math/math_c23_hypot3_f16.c.o
[100%] Built target cplus_hardened_libc
```

### Remaining blockers

None on this host. See `C23_FARITH_CLOSURE.md` §8 for the (non-blocking,
cross-portability) note about builtin-guarded definitions in `math_c23.c`.
