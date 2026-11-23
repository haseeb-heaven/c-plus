# C23 hypot3 / `_Float16` math APIs — additive implementation record

**Date:** 2026-11-23
**Branch:** `agent/modern-stdlib`
**Task:** add the five `math.h` APIs classified `MISSING` in
`docs/libc/iso_c_inventory.json` (rows at lines 3760–3816): the C23
three-argument `hypot(x,y,z)` overload, `frexp16`, `fma16`, `logb16`, `exp16`.

**Outcome: 5 of 5 implemented and tested.** All changes are additive; no
existing file was created, modified, staged or committed outside the four
files listed below. No build script, compiler, runtime or `third_party/` file
was touched.

---

## 1. Files created (strict ownership)

| File | Role |
|---|---|
| `libc/src/math/c23_hypot16.c` | Definitions of the five entry points |
| `libc/install-include/cplus_c23_hypot16.h` | Public declarations |
| `tests/libc/test_c23_hypot16.c` | Deterministic standalone test harness |
| `docs/libc/C23_HYPOT16.md` | This record |

No other file was created or modified. No `git add` / `git commit`.

### Symbol naming decision (important for the wiring agent)

This branch **already ships** `cplus_hypot3`, `cplus_frexp16`, `cplus_fma16`,
`cplus_logb16` and `cplus_exp16` from
`libc/src/math/math_c23_hypot3_f16.c`, and that file is already wired into
`libc/CMakeLists.txt` and `tests/libc/build_libc.sh`. To avoid duplicate
definitions if both translation units are linked, this file uses a distinct,
additive prefix:

| C23 API | New exported symbol |
|---|---|
| `hypot(x,y,z)` | `cplus_c23_hypot3` |
| `frexp16` | `cplus_c23_frexp16` |
| `fma16` | `cplus_c23_fma16` |
| `logb16` | `cplus_c23_logb16` |
| `exp16` | `cplus_c23_exp16` |

The additive link check in §6 proves both files coexist. The canonical
`cplus_*` names continue to be provided by the already-shipped file; the
public header here declares only the names this object actually defines.

---

## 2. Host capability evidence (exact commands and results)

Toolchain:

```
$ /usr/bin/clang --version
Apple clang version 16.0.0 (clang-1600.0.26.6)
Target: arm64-apple-darwin27.2.0
Thread model: posix
InstalledDir: /Library/Developer/CommandLineTools/usr/bin

$ uname -a
Darwin Haseebs-MacBook-Pro.local 27.2.0 Darwin Kernel Version 27.2.0: ... arm64
```

### 2.1 `_Float16` IS available — no fallback representation needed

Probe source (`/tmp/c23h16_probe_type.c`):

```c
#include <float.h>
#include <math.h>
_Float16 c23h16_probe(_Float16 x) { return (_Float16)((float)x + 1.0f); }
int main(void) { return (int)c23h16_probe((_Float16)1.0f); }
```

```
$ /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -c /tmp/c23h16_probe_type.c -o /tmp/c23h16_probe_type.o
EXIT=0
```

No diagnostic was emitted, with the exact required flags
`-std=c99 -Wall -Wextra -pedantic`. Also:

```
$ /usr/bin/clang -std=c99 -dM -E -x c /dev/null | grep -E '__FLT16_MANT_DIG__|__STDC_VERSION__'
#define __FLT16_MANT_DIG__ 11
#define __STDC_VERSION__ 199901L
```

A separate probe showed the `1.0f16` literal suffix is also accepted with
`-std=c99 -Wall -Wextra -pedantic` (EXIT=0), although the implementation and
tests deliberately use `(_Float16)float_literal` casts for portability.

Because `_Float16` is genuinely present, the conditional fallback described
in the task (e.g. `uint16_t` bit pattern or `float` with declared
non-conformance) was **not** needed and was **not** fabricated. The four f16
declarations are guarded by `#if defined(__FLT16_MANT_DIG__)`; on a compiler
where that macro is absent they are omitted rather than silently replaced by
a non-conforming type. No IEEE-754 binary16 bit-level semantics were invented.

### 2.2 Builtin availability (`__has_builtin`), `-std=c99 -Wall -Wextra -pedantic`

Probe source printed each `__has_builtin(...)` value:

```
__builtin_hypot3           = 0
__builtin_hypot            = 1
__builtin_hypotf16         = 0
__builtin_frexp16          = 0
__builtin_frexpf16         = 1
__builtin_fma16            = 0
__builtin_fmaf16           = 1
__builtin_logb16           = 0
__builtin_logbf16          = 0
__builtin_exp16            = 0
__builtin_expf16           = 1
```

(compile+run EXIT=0). Consequences: `fma16` and `exp16` can use genuine f16
compiler primitives; `frexp16` and `logb16` use exact f32 compositions; the
3-argument hypot has no host builtin.

### 2.3 The host has NO 3-argument `hypot` overload

`/tmp/c23h16_probe_hypot3.c`:

```c
#include <math.h>
int main(void) { double r = hypot(3.0, 4.0, 12.0); return r > 0.0 ? 0 : 1; }
```

```
$ /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -fsyntax-only /tmp/c23h16_probe_hypot3.c
/tmp/c23h16_probe_hypot3.c:2:45: error: too many arguments to function call, expected 2, have 3
    2 | int main(void) { double r = hypot(3.0, 4.0, 12.0); return r > 0.0 ? 0 : 1; }
      |                             ~~~~~           ^~~~
1 error generated.
EXIT=1
```

Therefore the "delegate to a host 3-arg overload" option is unavailable and
an explicit overflow-safe composition was implemented.

### 2.4 `__builtin_frexpf16` is unusable at `-O0` on this host

Probe body: `_Float16 r = __builtin_frexpf16((_Float16)3.0f, &e);`

```
$ /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -O0 /tmp/c23h16_probe_frexpf16.c -o /tmp/c23h16_probe_frexpf16_o0
fatal error: error in backend: Cannot select: 0x...: i32 = any_extend 0x...
  0x...: i16 = or 0x..., Constant:i16<14336>
  ...
clang: error: clang frontend command failed with exit code 70 (use -v to see invocation)
COMPILE_EXIT=1

$ /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -O2 /tmp/c23h16_probe_frexpf16.c -o /tmp/c23h16_probe_frexpf16_o2
COMPILE_EXIT=0
$ /tmp/c23h16_probe_frexpf16_o2
0.750000 2
```

`frexp16` therefore uses the exactly-equivalent `frexpf` + conversion path,
which compiles at every `-O` level (verified across the §6 matrix).

### 2.5 Provenance check — no pinned upstream source exists for these APIs

```
$ grep -rilE 'hypot3|frexp16|fma16|logb16|exp16|_Float16|binary16' third_party libc/upstream | head -50
EXIT=0   (no output: zero matching files)
```

`docs/libc/C23_MATH_UPSTREAM.md` records the pinned upstream as LLVM libc
`llvmorg-20.1.1` and states at line 178 that `hypot3` has no upstream
implementation at that pin; the f16 math paths would require C++
`src/__support` internals that are not vendored here.

No upstream body, constant, table, polynomial or bit trick was copied. The
3-argument hypot is an original scaling composition (§3.1); the four f16
bodies are thin delegations to real host primitives probed above.

---

## 3. Implementation

### 3.1 `cplus_c23_hypot3` — documented overflow-safe scaling

1. Infinity takes precedence over NaN, matching the host 2-arg `hypot`
   contract (verified: `hypot(INFINITY,NAN) == +inf` on this host), then NaN
   propagates.
2. `m = fmax(fmax(fabs(x), fabs(y)), fabs(z))`; `m == 0` returns `+0`.
3. `frexp(m, &e)` gives `m = f * 2^e`, `f` in `[0.5,1)`.
4. Each component is scaled by the exact power of two `2^-e` with `ldexp`
   and clamped with `fmin(.,1.0)`; the largest scaled component is `< 1`.
5. `sum = sx*sx + sy*sy + sz*sz < 3`, so `sqrt(sum)` cannot overflow.
6. `ldexp(sqrt(sum), e)` restores the magnitude; it overflows only if the
   true norm exceeds `DBL_MAX`, and rounds once on underflow.

The literal nesting `hypot(hypot(x,y),z)` would also be overflow-safe because
the host 2-arg `hypot` scales internally, but it applies two independent sqrt
roundings and, as §2.3 shows, there is no 3-arg host overload. The explicit
scaling keeps the intermediate bound (`< 3`) inspectable and testable at
`-O0`.

### 3.2 f16 helpers

| API | Implementation | Why |
|---|---|---|
| `frexp16` | `(_Float16)frexpf((float)x, exp)` | f16→f32 is lossless; the significand has ≤11 bits so the reverse conversion is exact; avoids the §2.4 backend crash |
| `fma16` | `__builtin_fmaf16` when `__has_builtin` is true, else `(_Float16)fma(double,double,double)` | genuine fused primitive; the fallback is correctly rounded because an 11×11-bit product plus an f16 addend fits exactly in binary64 |
| `logb16` | `(_Float16)logbf((float)x)` | `floor(log2|x|)` is an integer in `[-24,15]` for finite f16, exactly representable; no f16 logb builtin exists |
| `exp16` | `__builtin_expf16` when available, else `(_Float16)exp((double)x)` | genuine builtin; on this host it lowers via standard `expf` (no f16-specific external symbol) |

---

## 4. Host-probed exact f16 expectation values used in the tests

Collected with `/tmp/c23h16_probe_values.c` and confirmed to be IEEE-754
binary16 round-to-nearest-even results:

```
exp16(0.0) = 1.000000000000   exp16(1.0)  = 2.718750000000
exp16(-1.0)= 0.367919921875   exp16(2.0)  = 7.390625000000
exp16(-2.0)= 0.135375976562   exp16(0.5)  = 1.648437500000
fma16(1,1,2^-11) = 1.000000000000     (half-ulp tie -> 1)
fma16(1,1,2^-10) = 1.000976562500
logb16(2^-24) = -24.0   logb16(65504) = 15.0
frexp16(65504) = 0.999511718750 e=16
```

---

## 5. Semantic deviations and limitations (explicit)

1. **hypot3 rounding.** The sum of three squares and the square root each
   round once in double. The result is overflow-safe and finite whenever the
   true norm is representable, but may differ by a few ulp from a
   hypothetical single-step correctly-rounded 3-arg hypot. IEEE 754 does not
   define a 3-argument hypot operation; C23 does not mandate a specific
   rounding for this overload in all implementations. The test tolerance for
   the long-double differential sweep is 8 ulp.
2. **`exp16` lowering.** On this host `__builtin_expf16` lowers through the
   standard float `expf`; differential tests (65-point sweep plus six exact
   values) found no mismatch. A future host with a less accurate `expf`
   could in principle double-round; the fallback computes `exp` in double
   and rounds once to f16.
3. **`frexp16` / `logb16` use f32 host primitives** rather than native f16
   builtins. For `_Float16` inputs both paths are provably exact, so there is
   no numerical deviation; the only consequence is the standard `frexpf` /
   `logbf` dependency.
4. **Compiler guard.** If `__FLT16_MANT_DIG__` is undefined, the four f16
   functions are not emitted. This is a documented portability limitation,
   not a fabricated substitute; the probe in §2.1 proves the guard is active
   on the target host.
5. **Symbol names** differ from the already-shipped canonical `cplus_*`
   names by design (§1). No existing symbol is replaced, renamed or removed.

---

## 6. Verification (exact commands and results)

All commands run from the repository root.

### 6.1 Build and run (required flags, `timeout 30`)

```
$ /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -Ilibc/install-include \
    libc/src/math/c23_hypot16.c tests/libc/test_c23_hypot16.c -lm \
    -o /tmp/test_c23_hypot16
COMPILE_EXIT=0                       (no warnings, no errors)

$ timeout 30 /tmp/test_c23_hypot16
c23_hypot16: 357 0
c23_hypot16: ok
RUN_EXIT=0
```

The harness prints exactly `c23_hypot16: <checks> <failures>` and returns
non-zero if any check fails. Checks cover exact Pythagorean triples, signs,
`sqrt(3)`/`sqrt(338)`, 1e200 and 1e-200 magnitudes, `DBL_MAX`, smallest
subnormal, Inf/NaN precedence, a 25-point long-double differential sweep,
fixed and differential f16 expectations, fusion at the half-ulp tie, and
f16 Inf/NaN/special cases.

### 6.2 Flag/matrix re-run (all clean)

```
$ for STD in c99 c17 c2x; do for OPT in 0 2 3; do
    /usr/bin/clang -std=$STD -O$OPT -Wall -Wextra -pedantic \
      -Ilibc/install-include libc/src/math/c23_hypot16.c \
      tests/libc/test_c23_hypot16.c -lm -o /tmp/test_c23_hypot16_${STD}_O${OPT}
    timeout 30 /tmp/test_c23_hypot16_${STD}_O${OPT}
  done; done
c99  -O0 compile=0 run=0 :: c23_hypot16: 357 0
c99  -O2 compile=0 run=0 :: c23_hypot16: 357 0
c99  -O3 compile=0 run=0 :: c23_hypot16: 357 0
c17  -O0 compile=0 run=0 :: c23_hypot16: 357 0
c17  -O2 compile=0 run=0 :: c23_hypot16: 357 0
c17  -O3 compile=0 run=0 :: c23_hypot16: 357 0
c2x  -O0 compile=0 run=0 :: c23_hypot16: 357 0
c2x  -O2 compile=0 run=0 :: c23_hypot16: 357 0
c2x  -O3 compile=0 run=0 :: c23_hypot16: 357 0
```

### 6.3 `nm` evidence — new symbols only

```
$ /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -Ilibc/install-include \
    -c libc/src/math/c23_hypot16.c -o /tmp/c23_hypot16.o
COMPILE_EXIT=0

$ nm -g /tmp/c23_hypot16.o
00000000000005c4 T _cplus_c23_exp16
0000000000000570 T _cplus_c23_fma16
000000000000053c T _cplus_c23_frexp16
0000000000000000 T _cplus_c23_hypot3
0000000000000598 T _cplus_c23_logb16
                 U _expf
                 U _frexp
                 U _frexpf
                 U _ldexp
                 U _logbf
```

The object defines exactly five global symbols, all new `cplus_c23_*` names.
The `U` entries are standard C99 libm primitives present in macOS libSystem;
there are no f16-specific, project-internal or previously-owned symbols.
(Mach-O prefixes C symbols with `_`.)

### 6.4 Additive link with the already-shipped file

```
$ /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -Ilibc/install-include \
    libc/src/math/c23_hypot16.c \
    libc/src/math/math_c23_hypot3_f16.c libc/log/log.c \
    /tmp/c23h16_additive_main.c -lm -o /tmp/c23h16_additive
COMPILE_EXIT=0                       (no duplicate-definition error)
$ timeout 30 /tmp/c23h16_additive
old=13.0 new=13.0
RUN_EXIT=0
```

The driver calls both `cplus_hypot3` (old) and `cplus_c23_hypot3` (new).

### 6.5 Failure path and C++ header check

```
$ /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -Ilibc/install-include \
    libc/src/math/c23_hypot16.c /tmp/test_c23_hypot16_fail.c -lm \
    -o /tmp/test_c23_hypot16_fail      # copy with one expectation deliberately wrong
$ timeout 30 /tmp/test_c23_hypot16_fail
FAIL: FORCED FAILURE probe
c23_hypot16: 357 1
c23_hypot16: FAILED
FORCED_FAILURE_EXIT=1

$ /usr/bin/clang++ -std=c++17 -Wall -Wextra -pedantic -Ilibc/install-include \
    /tmp/c23h16_cpp.cpp libc/src/math/c23_hypot16.c -lm -o /tmp/c23h16_cpp
CXX_COMPILE_EXIT=0
$ timeout 30 /tmp/c23h16_cpp
CXX_RUN_EXIT=0
```

(The only C++-mode diagnostic was clang++'s `-Wdeprecated` notice about
treating a `.c` input as C++; the `extern "C"` header itself is clean.)

### 6.6 Naive-overflow detector (1e200)

```
$ /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -Ilibc/install-include \
    libc/src/math/c23_hypot16.c /tmp/c23h16_naive.c -lm -o /tmp/c23h16_naive
$ /tmp/c23h16_naive
naive=inf new=1.7320508075688773e+200 finite_new=1
```

This confirms that a naive `sqrt(x*x+y*y+z*z)` would fail the large-magnitude
test while the implemented scaling does not.

---

## 7. Integration note for the parent/wiring agent

Add `libc/src/math/c23_hypot16.c` to `LIBC_SOURCES` and the `build_libc.sh`
source loop if desired; add `#include "cplus_c23_hypot16.h"` to
`cplus_libc.h` or let consumers include it directly. No existing source,
header or build rule needs editing for the new names to be usable. If the
intent is instead to *replace* the earlier `math_c23_hypot3_f16.c`
implementation, that requires a separate, explicitly-approved rename of the
public symbols; this file deliberately does not perform it.
