# C23 math gaps — evidence and blocker register

**Date:** 2026-11-23
**Branch:** `agent/modern-stdlib`
**Owned files for this task:** `tests/family_gaps/math_gaps.c` and this file
only. No other file was created, modified, staged or committed.

## Result summary

| | |
|---|---|
| APIs classified `MISSING` for `math.h` in `docs/libc/iso_c_inventory.json` | **23** |
| APIs implemented / stubbed / emulated by this task | **0** |
| Explicit `SKIP <api>: <reason>` lines printed | **23** (names verified 1:1 against the inventory) |
| Nearest-equivalent exercises executed | **23** (one `EQUIV ... exercised` line per gap) |
| Real checks executed against available wrappers/equivalents | **1346** (`math_gaps: 1346 tested, 23 skipped`) |
| Distinct math wrappers covered by the differential section | **177** |
| Failures (plain build and UBSan build) | **0** |
| Exit codes (plain / UBSan) | **0 / 0** |

**No fake implementations were introduced.** The test never defines, calls or
links a `nextup`/`fadd`/`frexp16`/… entry point; each gap prints its exact
blocker and then exercises the nearest *available* host or `cplus_*`
equivalent (see the last column of the blocker table). Those exercises are
real, tolerance-checked tests, but they are explicitly **not** validation of
the missing C23 API.

## Source of truth

- `docs/libc/iso_c_inventory.json`, `headers[header == "math.h"].apis[]` where
  `classification == "MISSING"` — exactly 23 entries: `nextup/f/l`,
  `nextdown/f/l`, `fadd/f/l`, `fsub/f/l`, `fmul/f/l`, `fdiv/f/l`, the
  3-argument `hypot` overload, `frexp16`, `fma16`, `logb16`, `exp16`.
- `docs/libc/C23_MATH_UPSTREAM.md` — the pinned upstream audit whose exact
  dependency reasons are reproduced below.
- Pinned upstream: `llvm/llvm-project`, component `libc`, tag
  **`llvmorg-20.1.1`**, license **Apache-2.0 WITH LLVM-exception**
  (<https://llvm.org/LICENSE.txt>). URLs below are the exact
  `raw.githubusercontent.com` paths inspected on 2026-11-23.

Base URL for every wrapper row:
`https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-20.1.1/libc/`

## Per-API blocker table

| # | API (inventory name) | Exact upstream URL (pinned tag embedded in URL) | Dependency / contract blocker | Equivalent exercised in `math_gaps.c` |
|---:|---|---|---|---|
| 1 | `nextup` | `https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-20.1.1/libc/src/math/generic/nextup.cpp` | Thin wrapper over `fputil::nextupdown<false>` in `src/__support/FPUtil/ManipulationFunctions.h`; closure includes `FPBits.h`, `NormalFloat.h`, `dyadic_float.h`, `cast.h`, `rounding_mode.h`, `FEnvImpl.h`, `CPP/{bit,limits,type_traits}.h`, `hdr/math_macros.h`, `macros/*`. Core uses `FPBits<T>::uintval/min_subnormal/max_normal`. | `nextafter(x, +Inf)` vs `cplus_nextafter`; ±0 → `DBL_TRUE_MIN` |
| 2 | `nextupf` | `https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-20.1.1/libc/src/math/generic/nextupf.cpp` | Same `fputil::nextupdown<false>` C++ template / `__support` closure. | `nextafterf(x, +Inf)` vs `cplus_nextafterf`; ±0 → `FLT_TRUE_MIN` |
| 3 | `nextupl` | `https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-20.1.1/libc/src/math/generic/nextupl.cpp` | Same `fputil::nextupdown<false>` C++ template / `__support` closure. | `nextafterl(x, +Inf)` vs `cplus_nextafterl`; ±0 → `LDBL_TRUE_MIN` |
| 4 | `nextdown` | `https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-20.1.1/libc/src/math/generic/nextdown.cpp` | Thin wrapper over `fputil::nextupdown<true>` in `ManipulationFunctions.h`; same `__support` closure. | `nextafter(x, -Inf)` vs `cplus_nextafter`; −0 → `−DBL_TRUE_MIN` |
| 5 | `nextdownf` | `https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-20.1.1/libc/src/math/generic/nextdownf.cpp` | Same `fputil::nextupdown<true>` C++ template / `__support` closure. | `nextafterf(x, -Inf)` vs `cplus_nextafterf`; −0 → `−FLT_TRUE_MIN` |
| 6 | `nextdownl` | `https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-20.1.1/libc/src/math/generic/nextdownl.cpp` | Same `fputil::nextupdown<true>` C++ template / `__support` closure. | `nextafterl(x, -Inf)` vs `cplus_nextafterl`; −0 → `−LDBL_TRUE_MIN` |
| 7 | `fadd` | `https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-20.1.1/libc/src/math/generic/fadd.cpp` | `fputil::generic::add<float>` from `src/__support/FPUtil/generic/add_sub.h` (`DyadicFloat`, `FPBits`, `cpp::bit_ceil`, `quick_get_round`, `cast`). **Also a contract mismatch:** `src/math/fadd.h` declares `float fadd(double,double)`, C23 requires `double fadd(double,double)`. | host `a + b` cross-checked with `cplus_fma(a, 1.0, b)` |
| 8 | `faddf` | `https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-20.1.1/libc/src/math/generic/faddf.cpp` — **HTTP 404 at this tag** | No single-precision upstream entry point exists; the `add_sub.h` C++ closure is blocked as for row 7. | `float a + b` cross-checked with `cplus_fmaf(a, 1, b)` |
| 9 | `faddl` | `https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-20.1.1/libc/src/math/generic/faddl.cpp` | `fputil::generic::add<float>` (`add_sub.h`) returns `float` from `long double` args; C23 requires `long double faddl(long double,long double)`. | `long double a + b` cross-checked with `cplus_fmal(a, 1, b)` |
| 10 | `fsub` | `https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-20.1.1/libc/src/math/generic/fsub.cpp` | `fputil::generic::sub<float>` (`add_sub.h`) plus the same signature mismatch vs C23 `double fsub(double,double)`. | host `a - b` cross-checked with `cplus_fma(b, -1.0, a)` |
| 11 | `fsubf` | `https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-20.1.1/libc/src/math/generic/fsubf.cpp` — **HTTP 404 at this tag** | No single-precision upstream entry point; `add_sub.h` C++ closure blocked. | `float a - b` cross-checked with `cplus_fmaf(b, -1, a)` |
| 12 | `fsubl` | `https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-20.1.1/libc/src/math/generic/fsubl.cpp` | `fputil::generic::sub<float>` (`add_sub.h`) plus signature mismatch vs C23 `long double fsubl`. | `long double a - b` cross-checked with `cplus_fmal(b, -1, a)` |
| 13 | `fmul` | `https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-20.1.1/libc/src/math/generic/fmul.cpp` | `fputil::generic::mul<float>` plus `fputil::exact_mult`/`DoubleDouble` from `src/__support/FPUtil/{mul.h,double_double.h}`, `FPBits`, `hdr/errno_macros.h`, `hdr/fenv_macros.h`; signature mismatch vs C23. | host `a * b` cross-checked with `cplus_fma(a, b, 0.0)` |
| 14 | `fmulf` | `https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-20.1.1/libc/src/math/generic/fmulf.cpp` — **HTTP 404 at this tag** | No single-precision upstream entry point; `mul.h` / `double_double.h` C++ closure blocked. | `float a * b` cross-checked with `cplus_fmaf(a, b, 0)` |
| 15 | `fmull` | `https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-20.1.1/libc/src/math/generic/fmull.cpp` | `fputil::generic::mul<float>` (`mul.h`, `DyadicFloat`, `quick_mul`); signature mismatch vs C23 `long double fmull`. | `long double a * b` cross-checked with `cplus_fmal(a, b, 0)` |
| 16 | `fdiv` | `https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-20.1.1/libc/src/math/generic/fdiv.cpp` | `fputil::generic::div<float>` from `src/__support/FPUtil/generic/div.h` (`DyadicFloat`, `FPBits`, long-division loop); signature mismatch vs C23. | host `a / b` cross-checked with `cplus_fma(a, 1/b, 0.0)` |
| 17 | `fdivf` | `https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-20.1.1/libc/src/math/generic/fdivf.cpp` — **HTTP 404 at this tag** | No single-precision upstream entry point; `div.h` C++ closure blocked. | `float a / b` cross-checked with `cplus_fmaf(a, 1/b, 0)` |
| 18 | `fdivl` | `https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-20.1.1/libc/src/math/generic/fdivl.cpp` | `fputil::generic::div<float>` (`div.h`); signature mismatch vs C23 `long double fdivl`. | `long double a / b` cross-checked with `cplus_fmal(a, 1/b, 0)` |
| 19 | `hypot (3-argument C23 overload)` | `https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-20.1.1/libc/src/math/generic/hypot.cpp` and `https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-20.1.1/libc/src/math/hypot.h`; `https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-20.1.1/libc/src/math/generic/hypot3.cpp` — **HTTP 404 at this tag** | Upstream is 2-arg only (`fputil::hypot` from `src/__support/FPUtil/Hypot.h`) and `src/math/hypot.h` declares `hypot(double,double)`. There is no 3-arg source to copy — blocked independently of the `FPUtil` dependency. | `hypot(hypot(x,y),z)` vs `cplus_hypot(cplus_hypot(x,y),z)`, `(3,4,12) → 13`, Inf/NaN propagation |
| 20 | `frexp16` | `https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-20.1.1/libc/src/math/generic/frexpf16.cpp` | `fputil::frexp` in `ManipulationFunctions.h` → `FPBits`, `NormalFloat` bit-field reclassification. No `_Float16` type on this toolchain (`llvmorg-20.1.1` uses LLVM's `f16`-suffix naming). | `frexp(0.75)` / `frexpf(1.5f)` (both binary16-exact) vs `cplus_frexp` / `cplus_frexpf`, mantissa + exponent |
| 21 | `fma16` | `https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-20.1.1/libc/src/math/generic/f16fma.cpp` (closest match); `https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-20.1.1/libc/src/math/generic/fmaf16.cpp` — **HTTP 404 at this tag** | `fputil::fma<float16>` from `src/__support/FPUtil/FMA.h`; upstream signature `float16 f16fma(double,double,double)` ≠ C23 `_Float16 fma16(_Float16,_Float16,_Float16)` — dependency **and** contract blocker. | `fma(1.5,2.0,0.5)` / `fmaf(1.5f,2.0f,0.5f)` vs `cplus_fma` / `cplus_fmaf` |
| 22 | `logb16` | `https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-20.1.1/libc/src/math/generic/logbf16.cpp` | `fputil::logb` in `ManipulationFunctions.h` → `FPBits`, `DyadicFloat::get_unbiased_exponent`. | `logb(8.0)=3`, `logb(0)=−Inf`, `logbf(8.0f)=3` vs `cplus_logb` / `cplus_logbf` |
| 23 | `exp16` | `https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-20.1.1/libc/src/math/generic/expf16.cpp` (plus `https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-20.1.1/libc/src/math/generic/expxf16.h`) | Not a wrapper: the full algorithm needs `FPBits<float16>`, `ExceptValues`, `polyeval`, `cast`, `quick_get_round`, `raise/set_errno_if_required`, and `exp_range_reduction()` with two exception tables from `expxf16.h`. | `exp(0.5)` / `expf(0.5f)` vs `cplus_exp` / `cplus_expf`, tolerance-checked |

### Shared dependency-closure URLs (all `llvmorg-20.1.1`)

| Header | Exact URL | Used by |
|---|---|---|
| `ManipulationFunctions.h` | `https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-20.1.1/libc/src/__support/FPUtil/ManipulationFunctions.h` | rows 1–6, 20, 22 |
| `generic/add_sub.h` | `https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-20.1.1/libc/src/__support/FPUtil/generic/add_sub.h` | rows 7–12 |
| `generic/mul.h` | `https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-20.1.1/libc/src/__support/FPUtil/generic/mul.h` | rows 13–15 |
| `generic/div.h` | `https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-20.1.1/libc/src/__support/FPUtil/generic/div.h` | rows 16–18 |
| `double_double.h` | `https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-20.1.1/libc/src/__support/FPUtil/double_double.h` | rows 13–14 |
| `Hypot.h` | `https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-20.1.1/libc/src/__support/FPUtil/Hypot.h` | row 19 |
| `FMA.h` | `https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-20.1.1/libc/src/__support/FPUtil/FMA.h` | row 21 |
| `FPBits.h` | `https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-20.1.1/libc/src/__support/FPUtil/FPBits.h` | rows 1–6, 7–18, 20–23 |
| `expxf16.h` | `https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-20.1.1/libc/src/math/generic/expxf16.h` | row 23 |
| `fadd.h` | `https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-20.1.1/libc/src/math/fadd.h` | row 7 (signature proof) |
| `hypot.h` | `https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-20.1.1/libc/src/math/hypot.h` | row 19 (2-arg signature proof) |

None of these helpers is a libc function this project exposes as `cplus_*`, so
the integration rule ("copy only when the closure is self-contained or uses
functions we already provide") rejects all 23.

## Differential section — what was compared

`tests/family_gaps/math_gaps.c` contains one differential section covering
**177 distinct linkable math wrappers** in
`libc/install-include/cplus_libc.h`, each against the identically named host
`<math.h>` function:

| Family | Wrappers | Inputs per wrapper |
|---|---:|---|
| Unary floating, `double` / `float` / `long double` | 33 + 33 + 33 | NaN, +Inf, −Inf, +0, −0, plus a per-function domain/negative value (`sqrt/log/log2/log10/log1p/acosh/tgamma/lgamma`: −1 or −2; `asin/acos/atanh`: 2) |
| Binary floating (`pow`, `fmod`, `atan2`, `hypot`, `fmin`, `fmax`, `copysign`, `nextafter`, `fdim`, `remainder`) | 10 + 10 + 10 | 9 pairs: (NaN,1), (1,NaN), (+Inf,1), (1,+Inf), (−Inf,1), (1,−Inf), (+0,1), (−0,1), (−1,2) |
| Ternary `fma` / `fmaf` / `fmal` | 3 | NaN, Inf×0, 0×Inf, −0+0, cancellation, binary16-exact finite triple |
| Integer-exponent (`ldexp`, `scalbn`, `scalbln`) | 2 + 2 + 2 + 3 | special `x` × exponents {0, 10, −10, 1000} (long for `scalbln`) |
| Integer-result (`ilogb`, `lrint`, `llrint`, `lround`, `llround`) | 5 + 5 + 5 | NaN, ±Inf, ±0, −1 (impl-defined domain returns compared against host, not assumed) |
| Out-parameter (`frexp`, `modf`, `remquo`) | 3 + 3 + 3 | NaN, ±Inf, ±0, 1.5; exponents/quotients compared only where deterministic |
| Special (`nan`, `nexttoward`, `exp10`, `roundeven`) | 3 + 3 + 3 + 3 | NaN/Inf/−Inf/±0 plus finite cases; `exp10` oracle is `pow(10,x)` (no host symbol on this SDK); `roundeven` oracle is `nearbyint` under `FE_TONEAREST` (no host symbol) |

Comparison rule: NaN matches NaN by class (payload is unspecified), ±Inf and
±0 are compared exactly (sign included), and finite values use relative
tolerances `1e-12` (`double`/`long double`) and `1e-5` (`float`). Wrappers in
this library delegate to host libm, so the comparison is a parity check, not a
reimplementation.

Excluded from the 177: fenv/complex/time/locale wrappers (not double/float/
long-double math arithmetic), and the 23 missing APIs (no symbol to call).

## Verification

Build and run performed from the repository root with the pre-existing
`build/libc/libcplus_hardened.a` (that archive was **not** rebuilt or modified):

```sh
/usr/bin/clang -std=c11 -Wall -Wextra -Ilibc -Ilibc/install-include \
  tests/family_gaps/math_gaps.c build/libc/libcplus_hardened.a -lm -pthread \
  -o /tmp/math_gaps_test
/tmp/math_gaps_test

/usr/bin/clang -std=c11 -Wall -Wextra -fsanitize=undefined \
  -fno-sanitize-recover=all -Ilibc -Ilibc/install-include \
  tests/family_gaps/math_gaps.c build/libc/libcplus_hardened.a -lm -pthread \
  -o /tmp/math_gaps_ubsan
UBSAN_OPTIONS=print_stacktrace=1 /tmp/math_gaps_ubsan
```

Observed results:

- Both compiles: **no warnings, no errors** (`-Wall -Wextra` clean).
- Both runs printed 23 `SKIP` lines, 23 `EQUIV ... exercised` lines, **no**
  `FAIL` / UBSan `runtime error`, and:

```
math_gaps: 1346 tested, 23 skipped
math_gaps: differential wrappers covered: 177
math_gaps: ok
```

- Exit code **0** for the plain binary and **0** for the UBSan binary; output
  was identical, including the tested count.
- An independent script confirmed the 23 `SKIP` API names are exactly the 23
  `MISSING` `math.h` entries in `docs/libc/iso_c_inventory.json` — no missing
  and no extra names.

## Limitations / handoff

- All 23 APIs remain `MISSING`. The SKIPs are deliberate and are not masked
  failures; the nearest-equivalent exercises must not be read as "the C23 API
  was tested".
- `_Float16` is not available from this host toolchain, so the four `_Float16`
  gaps use binary16-exact `double`/`float` operands with tolerance; this is
  the narrowest honest equivalent available here.
- `tests/family_gaps/math_gaps.c` is not registered in any CMake/CTest list;
  this task grants ownership of only the two named files. A parent agent may
  wire it into the test runner if desired.
- No `git add` / commit was run, and `compiler/` and `third_party/` were not
  touched.
