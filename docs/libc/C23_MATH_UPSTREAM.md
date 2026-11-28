# C23 math APIs — pinned upstream copy audit

**Date:** 2026-11-23
**Branch:** `agent/modern-stdlib`
**Scope:** the 23 `math.h` APIs classified `MISSING` in `docs/libc/iso_c_inventory.json`:
`nextup/f/l`, `nextdown/f/l`, `fadd/f/l`, `fsub/f/l`, `fmul/f/l`, `fdiv/f/l`,
the 3-argument `hypot` overload, and `frexp16`, `fma16`, `logb16`, `exp16`.
The inventory currently lists exactly these 23 — there are no additional
`math.h` MISSING entries beyond the task list.

## Outcome

| | |
|---|---|
| APIs closed by copying upstream | **0** |
| APIs still blocked | **23** |
| Source / test / doc | `libc/src/math/math_c23_upstream.c`, `tests/libc/test_math_c23_upstream.c`, this file |

**No API was copied, stubbed, or emulated.** The integration rule for this task
was: *copy only when the upstream dependency closure is self-contained or
depends solely on libc functions we already provide (`cplus_nextafter`,
`cplus_fabs`, `cplus_scalbn`, …); if it needs LLVM libc C++ internals
(`src/__support/...`), do not copy — record the exact blocker.* Every one of
the 23 upstream implementations is a thin wrapper around a C++ template in
`libc/src/__support/FPUtil/`, so **all 23 are blocked**. `math_c23_upstream.c`
therefore mirrors the public surface with `extern` declarations only (same
policy as `math_c23.c`) and defines nothing; the test SKIPs each API with its
exact reason. No fake result and no reimplemented algorithm was introduced.

## Upstream source and license

- **Project:** `llvm/llvm-project`, component `libc`
- **Pinned tag:** `llvmorg-20.1.1`
- **License:** Apache-2.0 WITH LLVM-exception (the SPDX header of every file
  below; <https://llvm.org/LICENSE.txt>)
- **Retrieval method:** HTTP GET of `raw.githubusercontent.com` URLs, 2026-11-23
- **Base URL:** `https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-20.1.1/libc/`

Every `.cpp` impl lives under `libc/src/math/generic/`; every helper pulled in
lives under `libc/src/__support/FPUtil/` or `libc/src/__support/CPP/`.

### Exact URLs inspected

Wrappers (function bodies):

| C23 API | Upstream wrapper URL (base + path) |
|---|---|
| `nextup` / `nextupf` / `nextupl` | `libc/src/math/generic/nextup.cpp`, `nextupf.cpp`, `nextupl.cpp` |
| `nextdown` / `nextdownf` / `nextdownl` | `libc/src/math/generic/nextdown.cpp`, `nextdownf.cpp`, `nextdownl.cpp` |
| `fadd` / `faddl` | `libc/src/math/generic/fadd.cpp`, `faddl.cpp` |
| `fsub` / `fsubl` | `libc/src/math/generic/fsub.cpp`, `fsubl.cpp` |
| `fmul` / `fmull` | `libc/src/math/generic/fmul.cpp`, `fmull.cpp` |
| `fdiv` / `fdivl` | `libc/src/math/generic/fdiv.cpp`, `fdivl.cpp` |
| `hypot` (2-arg only) | `libc/src/math/generic/hypot.cpp`, `hypotf.cpp` |
| `frexp16` | `libc/src/math/generic/frexpf16.cpp` |
| `fma16` (closest match) | `libc/src/math/generic/f16fma.cpp` |
| `logb16` | `libc/src/math/generic/logbf16.cpp` |
| `exp16` | `libc/src/math/generic/expf16.cpp` |

Dependency-closure headers inspected:

- `libc/src/__support/FPUtil/ManipulationFunctions.h`
  (`fputil::nextupdown`, `fputil::frexp`, `fputil::logb`, `fputil::nextafter`,
  `fputil::ldexp`)
- `libc/src/__support/FPUtil/generic/add_sub.h` (`fputil::generic::add/sub`)
- `libc/src/__support/FPUtil/generic/mul.h` (`fputil::generic::mul`)
- `libc/src/__support/FPUtil/generic/div.h` (`fputil::generic::div`)
- `libc/src/math/fadd.h` (signature check), `libc/src/math/hypot.h`
  (signature check)
- `libc/src/math/generic/expf16.h`-adjacent `expxf16.h` (referenced by
  `expf16.cpp` for `exp_range_reduction` and exception tables)

Confirmed absent at this tag (HTTP 404): `generic/faddf.cpp`, `fsubf.cpp`,
`fmulf.cpp`, `fdivf.cpp`, `fmaf16.cpp`, `hypotf16.cpp`, `hypot3.cpp`,
`frexp16.cpp`, `logb16.cpp`, `exp16.cpp`. The last four are simply LLVM's
`f16`-suffix naming (`frexpf16`, `logbf16`, `expf16`); `hypot3.cpp` genuinely
does not exist.

## Per-API blockers (exact)

Every blocker below is a `src/__support/...` C++ dependency unless noted.
None of the called helpers is a libc function this project exposes as `cplus_*`.

| C23 API | Upstream call / file | Exact blocker |
|---|---|---|
| `nextup` | `fputil::nextupdown<false>` in `nextup.cpp` | Template in `ManipulationFunctions.h`; includes `FPBits.h`, `NormalFloat.h`, `dyadic_float.h`, `cast.h`, `rounding_mode.h`, `FEnvImpl.h`, `CPP/{bit,limits,type_traits}.h`, `hdr/math_macros.h`, `macros/*`. Core uses `FPBits<T>::uintval/min_subnormal/max_normal`. |
| `nextupf` | `fputil::nextupdown<false>` in `nextupf.cpp` | same template / closure |
| `nextupl` | `fputil::nextupdown<false>` in `nextupl.cpp` | same template / closure |
| `nextdown` | `fputil::nextupdown<true>` in `nextdown.cpp` | same template / closure |
| `nextdownf` | `fputil::nextupdown<true>` in `nextdownf.cpp` | same template / closure |
| `nextdownl` | `fputil::nextupdown<true>` in `nextdownl.cpp` | same template / closure |
| `fadd` | `fputil::generic::add<float>` in `fadd.cpp` → `add_sub.h` | `DyadicFloat`, `FPBits`, `cpp::bit_ceil`, `quick_get_round`, `cast`. **Also signature mismatch:** upstream `float fadd(double,double)` (`src/math/fadd.h`) vs C23 `double fadd(double,double)`. |
| `faddf` | none | `generic/faddf.cpp` is HTTP 404 — no single-precision entry point exists; plus same `add_sub.h` closure. |
| `faddl` | `fputil::generic::add<float>` in `faddl.cpp` | same `add_sub.h` closure; upstream returns `float` from `long double` args, not C23 `double faddl(long double,long double)`. |
| `fsub` | `fputil::generic::sub<float>` in `fsub.cpp` | `add_sub.h` closure; signature mismatch vs C23. |
| `fsubf` | none | `generic/fsubf.cpp` is HTTP 404; same closure. |
| `fsubl` | `fputil::generic::sub<float>` in `fsubl.cpp` | `add_sub.h` closure; signature mismatch vs C23. |
| `fmul` | `fputil::generic::mul<float>` + `fputil::exact_mult`/`DoubleDouble`/`Sign` in `fmul.cpp` | `mul.h`, `double_double.h`, `FPBits`, `hdr/errno_macros.h`, `hdr/fenv_macros.h`; signature mismatch vs C23. |
| `fmulf` | none | `generic/fmulf.cpp` is HTTP 404; same closure. |
| `fmull` | `fputil::generic::mul<float>` in `fmull.cpp` | `mul.h` (`DyadicFloat`, `quick_mul`); signature mismatch vs C23. |
| `fdiv` | `fputil::generic::div<float>` in `fdiv.cpp` → `div.h` | `DyadicFloat`, `FPBits`, long division loop; signature mismatch vs C23. |
| `fdivf` | none | `generic/fdivf.cpp` is HTTP 404; same closure. |
| `fdivl` | `fputil::generic::div<float>` in `fdivl.cpp` | `div.h` closure; signature mismatch vs C23. |
| `hypot` (3-arg) | none | `generic/hypot.cpp` is 2-arg only (`fputil::hypot` from `FPUtil/Hypot.h`), and `src/math/hypot.h` declares `hypot(double,double)`. No `hypot3.cpp` / 3-arg overload at this tag — there is no upstream source to copy (independent of the `FPUtil` blocker). |
| `frexp16` | `fputil::frexp` in `frexpf16.cpp` | `ManipulationFunctions.h` → `FPBits`, `NormalFloat` bit-field reclassification. |
| `fma16` | closest is `f16fma(double,double,double)` in `f16fma.cpp` | `fputil::fma<float16>` from `FPUtil/FMA.h`; `generic/fmaf16.cpp` is HTTP 404; upstream signature is `float16 f16fma(double,double,double)` vs C23 `_Float16 fma16(_Float16,_Float16,_Float16)` — both a dependency and a signature blocker. |
| `logb16` | `fputil::logb` in `logbf16.cpp` | `ManipulationFunctions.h` → `FPBits`, `DyadicFloat::get_unbiased_exponent`. |
| `exp16` | full algorithm in `expf16.cpp` | `FPBits<float16>`, `ExceptValues`, `polyeval`, `cast`, `quick_get_round`, `raise/set_errno_if_required`, plus `exp_range_reduction()` and two exception tables from `expxf16.h`. Not a wrapper — the entire algorithm and tables would have to be vendored. |

### Why the `fadd`/`fsub`/`fmul`/`fdiv` family cannot be reused even as-is

LLVM libc at this tag uses these names for **correctly-rounded narrowing**
operations: `float fadd(double,double)`, `float fsub(double,double)`,
`float fmul(double,double)`, `float fdiv(double,double)` (and `f*l` variants
producing `float` from `long double`). C23 specifies same-width signatures:
`double fadd(double,double)`, `double faddl(long double,long double)`, etc.
The `/f` single-precision C23 names have no upstream file at all. So this
family is blocked twice: by the `__support` C++ templates and by an
incompatible public contract.

## What would unblock these

Out of scope for this task (the rule forbids importing LLVM libc C++
internals), recorded for a future decision: vendoring
`src/__support/FPUtil/{FPBits,NormalFloat,dyadic_float,FMA,Hypot,cast,rounding_mode,
BasicOperations,double_double,ExceptValues,PolyEval}.h`, the `generic/{add_sub,mul,div}.h`
templates, `expxf16.h`, and the `src/__support/CPP/*`, `hdr/*`, `macros/*`
layers — or choosing a different upstream with self-contained C sources.
Alternatively, host `__builtin_*` wrappers are already covered, where
available, by `libc/src/math/math_c23.c`.

## Verification performed

Direct compile + link of source and test (per task), then run:

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
/usr/bin/clang -std=c99 -Ilibc -Ilibc/include -Ilibc/install-include \
  libc/src/math/math_c23_upstream.c tests/libc/test_math_c23_upstream.c \
  build/libc/libcplus_hardened.a -lm -pthread -o /tmp/mcu_test
/tmp/mcu_test
```

Result: compiled and linked with no errors; test printed 23 explicit
`SKIP` lines and terminated with:

```
math_c23_upstream: harness ok; APIs tested=0, skipped=23
math_c23_upstream: ok
```

Additional checks:

- `-Wall -Wextra`: **no warnings** (warnings from literal `/*` inside block
  comments in an earlier draft were removed).
- UBSan: `-fsanitize=undefined -fno-sanitize-recover=all` build ran with the
  identical output and exit code 0.
- No files outside the three owned paths were created or modified; no
  `git add` / commit was run; `compiler/` was not touched.

### Not part of the library build (owner action required)

`libc/src/math/math_c23_upstream.c` is intentionally **not** listed in
`libc/CMakeLists.txt`, because this task grants ownership of only the three
files above and forbids editing others. It currently compiles standalone via
the command above. If this audit file should be part of
`libcplus_hardened.a`, the parent agent must add it to the `libc/CMakeLists.txt`
math source list. It emits no symbols (externs only), so adding it is
link-safe, but it also defines no API.

## Warnings / caveats

- All 23 APIs remain MISSING in the built library; this work closes none of
  them. The SKIPs are deliberate, not failures masked as passes.
- LLVM libc's `fadd`/`fsub`/`fmul`/`fdiv` and `f16fma` names are not the C23
  signatures at this tag — copying them would have produced a misleading
  "closed" status. That mismatch is recorded rather than worked around.
- `hypot3` has no upstream implementation at all at `llvmorg-20.1.1`.
