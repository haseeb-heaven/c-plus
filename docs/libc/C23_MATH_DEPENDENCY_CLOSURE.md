# C23 math APIs — exact dependency closure and unblock conditions

**Date:** 2026-11-23
**Branch:** `agent/modern-stdlib`
**Scope:** the 23 `math.h` APIs classified `MISSING` in `docs/libc/iso_c_inventory.json`,
plus the four adjacent C23 blockers called out in the task (`timespec_getres`,
`FE_TIESAWAY`, `mbrtoc8`, `c8rtomb`). **27 APIs documented in total.**
**Ownership:** this file only. No source, header, contract, or CMake file was
created, modified, staged, or committed by this task.

This document records *why* the pinned upstream implementations cannot be
imported under this repository's integration rule. It closes no API. It defines
no body, no constant, and no algorithm; see §6.

---

## 1. Pinned revisions (apply to every path in this document)

| Component | Pin | Exact SHA | License | Local evidence |
|---|---|---|---|---|
| LLVM libc | tag `llvmorg-20.1.1` | tag object `46174b7e09f5a975e582d35f6674cd31cae414f9` → **commit `424c2d9b7e4de40d0804dd374721e6411c27d1d1`** | Apache-2.0 WITH LLVM-exception | `docs/libc/C23_MATH_UPSTREAM.md:30-42`; tag resolved 2026-11-23 via `api.github.com/repos/llvm/llvm-project/git/ref/tags/llvmorg-20.1.1` then `/git/tags/46174b7e…` |
| musl | pinned vendored commit | `7813e9119eb5fe681b6750b8a033a0490bca0941` | MIT | `libc/upstream/musl/UPSTREAM_COMMIT`, `docs/libc/UPSTREAM_EVALUATION.md:26` |
| This project's toolchain | `/usr/bin/clang` | Apple clang 16.0.0, `clang-1600.0.26.6`, target `arm64-apple-darwin27.2.0` | n/a | `docs/libc/C23_SMALL_UPSTREAM.md:23-28`, `libc/src/math/math_c23.c:316` |

For every upstream path below the full URL is
`https://raw.githubusercontent.com/llvm/llvm-project/424c2d9b7e4de40d0804dd374721e6411c27d1d1/libc/<path>`
(the historical audit used the equivalent `llvmorg-20.1.1` tag spelling). The
one known per-file blob SHA recorded in this repository is
`memset_explicit.cpp` = `a3e4dad874397463cb5315679f99a312ac3a53fe`
(`docs/libc/C23_SMALL_UPSTREAM.md:19`); that file is **not** one of the 23 math
APIs and its body was not copied here. Per-file blob SHAs for the 23 wrappers
were not pinned by the earlier audits; the authoritative pin is the tag's
commit SHA above.

**Inventory truth check.** `docs/libc/iso_c_inventory.json` contains exactly 23
`classification == "MISSING"` entries under `math.h`: `nextup/f/l`,
`nextdown/f/l`, `fadd/f/l`, `fsub/f/l`, `fmul/f/l`, `fdiv/f/l`, the 3-argument
`hypot` overload, `frexp16`, `fma16`, `logb16`, `exp16`. The header's
`by_classification` metadata is inconsistent with its own `apis[]` array — see
§7.2 — so the count of 23 was taken from the entries, not from that metadata.

---

## 2. Why the closure cannot be satisfied in this repository

The mandated integration rule (`docs/libc/C23_MATH_UPSTREAM.md:19-24`) is:
copy only when the upstream dependency closure is self-contained, or depends
solely on libc functions this project already provides (`cplus_nextafter`,
`cplus_fabs`, `cplus_scalbn`, …); if it needs LLVM libc C++ internals
(`src/__support/...`), do not copy — record the exact blocker.

All 23 fail that rule. The reasons are structural, not incidental:

1. **Language / build boundary.** Every available upstream body is a C++20
   translation unit: `namespace LIBC_NAMESPACE_DECL`, `LLVM_LIBC_FUNCTION`,
   function templates, `constexpr`, `if constexpr`, structured bindings and
   `cpp::array`. This project compiles its libc as C99/C11 (`-std=c99` /
   `-std=c11`) and has no C++ compilation unit in `libc/CMakeLists.txt`. A C
   translation unit cannot name `fputil::*` at all.
2. **Headers not vendored here.** The transitive closure is the whole
   `libc/src/__support/{FPUtil,CPP,macros,common.h}` stack plus `libc/include/`
   `hdr/*` and `llvm-libc-macros/*`. This repository vendored only a *subset* of
   musl (40 `.c` files, no math sources) and a few C headers; none of the LLVM
   libc C++ support tree is present. Vendoring it would be a new upstream
   import, not a copy of one file.
3. **No local equivalent to substitute.** None of the called helpers is a
   function this library exposes as `cplus_*`. Bit-level primitives
   (`FPBits<T>::uintval`, `min_subnormal`, `max_normal`), `DyadicFloat`,
   `ExceptValues`, `quick_mul`, `quick_get_round`, `polyeval` and the Sollya
   tables have no `cplus_*` counterpart, so the "uses functions we already
   provide" exception does not apply.
4. **Two families have no source at all.** `faddf`/`fsubf`/`fmulf`/`fdivf`
   (`generic/{faddf,fsubf,fmulf,fdivf}.cpp`) and the 3-argument `hypot`
   (`generic/hypot3.cpp`) are HTTP 404 at the pin. For these, no amount of
   C++ tooling helps: there is nothing to vendor.
5. **Two public contracts differ from C23.** LLVM's `fadd`/`fsub`/`fmul`/`fdiv`
   are *narrowing* operations (`float fadd(double,double)`,
   `float faddl(long double,long double)` — verified at the pin in
   `src/math/fadd.h` and `src/math/generic/fadd.cpp`/`faddl.cpp`), whereas C23
   specifies same-width results. `f16fma` has signature
   `float16 f16fma(double,double,double)` (verified at the pin), not
   `_Float16 fma16(_Float16,_Float16,_Float16)`.

A **newer toolchain** helps only where it supplies a `__builtin_*` for the API,
which removes the need for any upstream closure; Apple clang 16 exposes none
of these builtins (`libc/src/math/math_c23.c:12-28,117-121,179-184,296-302,315-319`).
An **approved C++ compilation unit** means: vendoring the full `__support` +
`hdr` + `macros` + `CPP` tree at commit `424c2d9…`, building it as C++20 with
LLVM libc's namespace/config macros, and adding a C ABI shim with the correct
C23 signature. Both routes require an explicit orchestrator decision and are
recorded as unblock conditions below; neither was taken here.

**Correction to a related claim.** The host *does* provide the `_Float16` type:
`/usr/bin/clang -std=c99 -dM -E -x c /dev/null | grep __FLT16_MANT_DIG__` yields
`#define __FLT16_MANT_DIG__ 11` (also stated in `libc/src/math/math_c23.c:316`).
Therefore the four `_Float16` APIs are blocked by the C++ `__support` closure
and, for `fma16`, the signature mismatch — **not** by an absent type. The
assertion to the contrary in `docs/libc/MATH_GAPS_EVIDENCE.md:67,159-161` is
incorrect (see §7.3). The declarations in `libc/src/math/math_c23_upstream.c`
are correctly guarded by `#if defined(__FLT16_MANT_DIG__)` and are therefore
active on this host.

### Shared closure bundles (referenced by the table)

| Tag | Transitive C++ template dependencies, all at commit `424c2d9…` |
|---|---|
| **MANIP** | `src/__support/FPUtil/ManipulationFunctions.h` → `FPBits.h`, `NormalFloat.h`, `dyadic_float.h`, `cast.h`, `rounding_mode.h`, `FEnvImpl.h`, `except_value_utils.h`, `src/__support/CPP/{bit,limits,type_traits}.h`, `hdr/math_macros.h`, `src/__support/macros/*`; core uses `FPBits<T>::uintval/min_subnormal/max_normal` |
| **ADD** | `src/__support/FPUtil/generic/add_sub.h` → `fputil::generic::add/sub`, `DyadicFloat`, `FPBits`, `cpp::bit_ceil`, `quick_get_round`, `cast` (+ **MANIP** base) |
| **MUL** | `src/__support/FPUtil/generic/mul.h` + `double_double.h` (`fputil::exact_mult`, `DoubleDouble`, `Sign`), `hdr/errno_macros.h`, `hdr/fenv_macros.h`, `FPBits`, `quick_mul` (+ **MANIP** base) |
| **DIV** | `src/__support/FPUtil/generic/div.h` → `DyadicFloat`, `FPBits`, long-division loop (+ **MANIP** base) |
| **HYPOT** | `src/__support/FPUtil/Hypot.h` → `fputil::hypot`, `FPBits` |
| **FMA** | `src/__support/FPUtil/FMA.h` → `fputil::fma<float16>`, `FPBits`, `rounding_mode`, `cast` |
| **EXP16** | `generic/expxf16.h` (`cpp::array`, `FPBits`, `PolyEval`, `cast`, `multiply_add`, `nearest_integer`, tables `EXP_HI`/`EXP_MID`, minimax polynomials) + `FPBits<float16>`, `ExceptValues<float16,2>` and `ExceptValues<float16,3>` (`except_value_utils.h`), `polyeval`, `cast`, `quick_get_round`, `raise/set_errno_if_required`, `hdr/errno_macros.h`, `hdr/fenv_macros.h`, `macros/optimization.h` |

---

## 3. The 23 blocked math APIs

Every row's upstream files are pinned at commit `424c2d9…` (§1). "C23 name" is
the inventory name; the corresponding declared-only symbol is `cplus_<name>`
(`cplus_hypot3` for the 3-argument overload).

| # | C23 API | Required upstream file(s) @ `424c2d9…` | Transitive C++ template dependencies | Exact reason the closure cannot be satisfied here | Unblock condition |
|---:|---|---|---|---|---|
| 1 | `nextup` | `src/math/generic/nextup.cpp`; **MANIP** | `fputil::nextupdown<false>`; `FPBits<double>`, `NormalFloat`, `dyadic_float` | C++20 template + unvendored `__support`/`hdr`/`macros`; bit-level `FPBits::uintval/min_subnormal/max_normal` has no `cplus_*` equivalent. C TU cannot instantiate it. | Vendor **MANIP** tree and add a C++20 TU + C ABI shim; **or** a newer toolchain providing `__builtin_nextup` (none on Apple clang 16) |
| 2 | `nextupf` | `src/math/generic/nextupf.cpp`; **MANIP** | same `fputil::nextupdown<false>` template | Same closure as row 1, `FPBits<float>` | Same as row 1 (or `__builtin_nextupf`) |
| 3 | `nextupl` | `src/math/generic/nextupl.cpp`; **MANIP** | same `fputil::nextupdown<false>` template | Same closure as row 1, `FPBits<long double>` | Same as row 1 (or `__builtin_nextupl`) |
| 4 | `nextdown` | `src/math/generic/nextdown.cpp`; **MANIP** | `fputil::nextupdown<true>` | Same closure as row 1 | Vendor **MANIP** + C++20 shim; or `__builtin_nextdown` |
| 5 | `nextdownf` | `src/math/generic/nextdownf.cpp`; **MANIP** | `fputil::nextupdown<true>` | Same closure as row 1 | Same as row 4 (or `__builtin_nextdownf`) |
| 6 | `nextdownl` | `src/math/generic/nextdownl.cpp`; **MANIP** | `fputil::nextupdown<true>` | Same closure as row 1 | Same as row 4 (or `__builtin_nextdownl`) |
| 7 | `fadd` | `src/math/generic/fadd.cpp`, `src/math/fadd.h`; **ADD** | `fputil::generic::add<float>`, `DyadicFloat`, `cpp::bit_ceil`, `quick_get_round`, `cast` | `__support` C++ closure **and** contract mismatch: upstream is `float fadd(double,double)`; C23 is `double fadd(double,double)`. | Vendor **ADD** + C++20 shim **and** instantiate/wrap the template to return `double` (signature adaptation), with rounding-mode tests |
| 8 | `faddf` | `src/math/generic/faddf.cpp` — **HTTP 404 at pin**; **ADD** would be needed | `fputil::generic::add<float>` (no upstream entry point exists) | No source file to copy at all; **ADD** closure also unsatisfied. | Different upstream with a self-contained C `faddf`, or a newer toolchain with `__builtin_faddf`. Vendoring `add_sub.h` alone does not create an entry point |
| 9 | `faddl` | `src/math/generic/faddl.cpp`; **ADD** | `fputil::generic::add<float>` | `__support` closure **and** contract mismatch: upstream is `float faddl(long double,long double)` (verified at pin); C23 is `long double faddl(long double,long double)`. | Vendor **ADD** + C++20 shim **and** adapt result type to `long double` |
| 10 | `fsub` | `src/math/generic/fsub.cpp`; **ADD** | `fputil::generic::sub<float>` | Same as row 7 (upstream `float fsub(double,double)` vs C23 `double fsub`) | Same as row 7 |
| 11 | `fsubf` | `src/math/generic/fsubf.cpp` — **HTTP 404 at pin**; **ADD** would be needed | `fputil::generic::sub<float>` (no upstream entry point) | Same as row 8 | Same as row 8 (or `__builtin_fsubf`) |
| 12 | `fsubl` | `src/math/generic/fsubl.cpp`; **ADD** | `fputil::generic::sub<float>` | Same as row 9 (upstream returns `float` from `long double` args) | Same as row 9 |
| 13 | `fmul` | `src/math/generic/fmul.cpp`; **MUL** | `fputil::generic::mul<float>`, `exact_mult`, `DoubleDouble`, `Sign`, `hdr/errno_macros.h`, `hdr/fenv_macros.h` | `__support` C++ closure **and** contract mismatch (`float fmul(double,double)` vs C23 `double fmul`) | Vendor **MUL** + C++20 shim + adapt result type to `double` |
| 14 | `fmulf` | `src/math/generic/fmulf.cpp` — **HTTP 404 at pin**; **MUL** would be needed | `fputil::generic::mul<float>`, `double_double.h` (no upstream entry point) | Same as row 8 | Same as row 8 (or `__builtin_fmulf`) |
| 15 | `fmull` | `src/math/generic/fmull.cpp`; **MUL** | `fputil::generic::mul<float>`, `DyadicFloat`, `quick_mul` | `__support` closure **and** contract mismatch vs C23 `long double fmull` | Vendor **MUL** + C++20 shim + adapt result type to `long double` |
| 16 | `fdiv` | `src/math/generic/fdiv.cpp`; **DIV** | `fputil::generic::div<float>`, `DyadicFloat`, `FPBits`, long-division loop | `__support` closure **and** contract mismatch (`float fdiv(double,double)` vs C23 `double fdiv`) | Vendor **DIV** + C++20 shim + adapt result type to `double` |
| 17 | `fdivf` | `src/math/generic/fdivf.cpp` — **HTTP 404 at pin**; **DIV** would be needed | `fputil::generic::div<float>` (no upstream entry point) | Same as row 8 | Same as row 8 (or `__builtin_fdivf`) |
| 18 | `fdivl` | `src/math/generic/fdivl.cpp`; **DIV** | `fputil::generic::div<float>` | `__support` closure **and** contract mismatch vs C23 `long double fdivl` | Vendor **DIV** + C++20 shim + adapt result type to `long double` |
| 19 | `hypot` (3-arg overload) | `src/math/generic/hypot.cpp`, `src/math/hypot.h`; `src/math/generic/hypot3.cpp` — **HTTP 404 at pin** | **HYPOT** (`fputil::hypot`, `FPBits`) | Upstream is 2-arg only: `src/math/hypot.h` declares `double hypot(double,double)` (verified at pin). No 3-arg source exists — blocked independently of the C++ dependency. Naive `sqrt(x*x+y*y+z*z)` is an invented algorithm with different overflow/rounding. | A different upstream with a genuine 3-arg implementation, or a newer toolchain with a 3-arg builtin. Vendoring `hypot.cpp` cannot satisfy this API |
| 20 | `frexp16` | `src/math/generic/frexpf16.cpp`; **MANIP** | `fputil::frexp`, `FPBits<float16>`, `NormalFloat` bit-field reclassification | `__support` C++ closure; host has `_Float16` but no C++ TU or vendored headers. LLVM name is `frexpf16`, not C23 `frexp16` | Vendor **MANIP** + C++20 TU using `float16`; add C ABI shim named `frexp16` with `_Float16` signature; or newer toolchain with `__builtin_frexp16` |
| 21 | `fma16` | `src/math/generic/f16fma.cpp` (closest match); `src/math/generic/fmaf16.cpp` — **HTTP 404 at pin**; **FMA** | `fputil::fma<float16>`, `FPBits`, `rounding_mode`, `cast` | `__support` closure **and** contract mismatch: upstream is `float16 f16fma(double,double,double)` (verified at pin), C23 is `_Float16 fma16(_Float16,_Float16,_Float16)`; no `fmaf16.cpp` entry point | Vendor **FMA** + C++20 shim **and** change both name and parameter/result types to `_Float16`; or newer toolchain with `__builtin_fma16` |
| 22 | `logb16` | `src/math/generic/logbf16.cpp`; **MANIP** | `fputil::logb`, `FPBits<float16>`, `DyadicFloat::get_unbiased_exponent` | `__support` C++ closure; LLVM name is `logbf16` | Vendor **MANIP** + C++20 TU + `logb16` C ABI shim; or newer toolchain with `__builtin_logb16` |
| 23 | `exp16` | `src/math/generic/expf16.cpp`, `src/math/generic/expxf16.h`; **EXP16** | Full algorithm (not a wrapper): `FPBits<float16>`, `ExceptValues<float16,2>`/`ExceptValues<float16,3>`, `polyeval`, `cast`, `quick_get_round`, `raise/set_errno_if_required`, `cpp::array`, `multiply_add`, `nearest_integer`, `exp_range_reduction()`, tables `EXP_HI`/`EXP_MID` and minimax coefficients | The entire algorithm and every generated table would have to be vendored through a C++20 TU; no `cplus_*` substitute for any primitive, and no host builtin | Vendor `expf16.cpp` + `expxf16.h` + **EXP16** closure in a C++20 TU and add a `_Float16 exp16(_Float16)` C ABI shim; or newer toolchain with `__builtin_exp16`. Copying the two generated tables alone would be fabricating a partial implementation |

---

## 4. The four adjacent C23 blockers (separate blocker classes)

These are **not** part of the 23 math APIs and are not solvable by the same
C++-TU route. They are blocked by *no pinned source* and/or *no host symbol*.

| API | Header / kind | Pinned source status | Host status | Exact blocker | Unblock condition |
|---|---|---|---|---|---|
| `timespec_getres` | `<time.h>`, function | No source at either pin: musl `v1.2.6`/`master` has only `timespec_get.c`; LLVM libc `llvmorg-20.1.1`/`main` has no `timespec_getres.{cpp,h}` and no CMake target (`C23_SMALL_UPSTREAM.md:20,63-68`) | Host `<time.h>` does not declare it; no host symbol (`C23_SMALL_UPSTREAM.md:23-28`) | Deriving it from `timespec_get`/`clock_getres` would be writing a new algorithm, which the method forbids | A pinned upstream revision that actually ships `timespec_getres`, or explicit authorisation to implement it against a host `clock_getres`-style primitive. No `cplus_timespec_getres` body is emitted today |
| `FE_TIESAWAY` | `<fenv.h>`, macro/constant | Not usable: LLVM libc's pinned `include/llvm-libc-macros/fenv-macros.h` defines `FE_DFL_ENV`, exception macros and `FE_DOWNWARD/TONEAREST/TOWARDZERO/UPWARD` but **not** `FE_TIESAWAY`; musl keeps rounding constants in per-arch `<bits/fenv.h>` (`C23_SMALL_UPSTREAM.md:69-72,51-53`) | Host `<fenv.h>` has no `FE_TIESAWAY`; AArch64 FPCR has no ties-away encoding (`C23_SMALL_UPSTREAM.md:23-28,128-134`) | It is a compiler-provided macro, not a function. Choosing a number would fabricate host capability. `CPLUS_C23_UPSTREAM_FE_TIESAWAY` is only a direct alias when the host defines it; otherwise the capability macro is 0 | A toolchain/host whose `<fenv.h>` defines `FE_TIESAWAY` (then the alias becomes active); a pinned header providing a portable value; or explicit authorisation to publish a value |
| `mbrtoc8` | `<uchar.h>`, function | No pinned musl revision: 404 at commit `7813e911…`, tag `v1.2.6`, `master` and bminor mirror; zero matching commits; vendored tree has no `src/multibyte/` (`UCHARC23_UPSTREAM.md:20-31`). Only an **unmerged** 2023 mailing-list patch exists (`8645e112…`/`0c73fa10…`), which was not pinned and raised correctness objections (`UCHARC23_UPSTREAM.md:40-64,90-120`) | Host `<uchar.h>` is not provided at all by this toolchain (`iso_c_inventory.json:16`); no host symbol | The only available code depends on musl-internal `mbstate_t` encoding (`(unsigned*)st`), has a non-conforming NULL-source path, needs `weak_alias`/`_Alignas`/`internal.h`, and has a known C-locale defect. Copying it would not build or would be a fabricated adaptation | A pinned musl revision that ships the functions, or explicit orchestrator authorisation to integrate the unmerged patch with the documented host-adaptation edits. No `cplus_mbrtoc8` body is emitted today |
| `c8rtomb` | `<uchar.h>`, function | Same as `mbrtoc8`: 404 at every pinned revision; same unmerged patch (`UCHARC23_UPSTREAM.md:20-31,126-160`) | Same as `mbrtoc8`: no host header, no host symbol | Same as `mbrtoc8`; additionally the patch relies on musl `wcrtomb` ignoring its state argument, while this repo's `cplus_wcrtomb` delegates to the host | Same as `mbrtoc8`. No `cplus_c8rtomb` body is emitted today |

---

## 5. Verification that will prove each API once implemented

Today these commands **pass with SKIPs**; that is the expected state. After an
implementation lands, the same commands are the proof gate — a SKIP line or a
non-zero `skipped` count means the API is still not closed.

### 5.1 The 23 math APIs

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
/usr/bin/clang -std=c99 -Wall -Wextra -Wpedantic \
  -Ilibc -Ilibc/include -Ilibc/install-include \
  libc/src/math/math_c23_upstream.c tests/libc/test_math_c23_upstream.c \
  build/libc/libcplus_hardened.a -lm -pthread -o /tmp/mcu_proof
/tmp/mcu_proof | tee /tmp/mcu_proof.log
! grep -q 'math_c23_upstream: SKIP' /tmp/mcu_proof.log
grep -q 'APIs tested=23, skipped=0' /tmp/mcu_proof.log
```

The harness prints `math_c23_upstream: SKIP <api> (<reason>)` per gap and ends
with `math_c23_upstream: harness ok; APIs tested=<n>, skipped=<m>` followed by
`math_c23_upstream: ok` (`tests/libc/test_math_c23_upstream.c:33,139-141`).
Proof requires zero SKIP lines and `APIs tested=23, skipped=0`.

If the chosen unblock route is an approved C++ compilation unit, also compile
the vendored closure with C++20 before linking:

```sh
/usr/bin/clang++ -std=c++20 -Wall -Wextra \
  -I<vendored-llvm-libc-root> -Ilibc -Ilibc/install-include \
  -c <new_cpp_implementation>.cpp -o /tmp/<new_cpp_implementation>.o
```

and confirm the C ABI shim exports the C23 signature (e.g.
`nm -g /tmp/<new_cpp_implementation>.o | grep cplus_faddl` shows
`long double faddl(long double, long double)` semantics, not `float`).

Full regression (after the integrating agent wires the new sources into the
build, which this task does not do):

```sh
cmake -S . -B build-macos-verify -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-macos-verify --parallel
ctest --test-dir build-macos-verify --output-on-failure
python3 tools/format_cpp.py --check
```

### 5.2 `timespec_getres` and `FE_TIESAWAY`

```sh
SYS=/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk
/usr/bin/clang -isysroot "$SYS" -std=c99 -Wall -Wextra -Wpedantic -Werror \
  -fsanitize=undefined -fno-omit-frame-pointer -g -Ilibc -Ilibc/log \
  libc/src/c23_upstream_small.c libc/log/log.c libc/src/string/memory.c \
  tests/libc/test_c23_upstream_small.c -o /tmp/test_c23_upstream
UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 /tmp/test_c23_upstream \
  | tee /tmp/test_c23_upstream.log
! grep -q '^SKIP timespec_getres:' /tmp/test_c23_upstream.log
! grep -q '^SKIP FE_TIESAWAY:' /tmp/test_c23_upstream.log
grep -q '^c23_upstream_small: ok' /tmp/test_c23_upstream.log
```

Current output is `SKIP timespec_getres: …`, `SKIP FE_TIESAWAY: …`,
`c23_upstream_small: ok` (`tests/libc/test_c23_upstream_small.c:244,249,253`).

### 5.3 `mbrtoc8` and `c8rtomb`

```sh
/usr/bin/clang -std=c99 -Wall -Wextra -pedantic -Werror \
  -Ilibc -Ilibc/log -Ilibc/include \
  libc/src/uchar/uchar_c23.c libc/log/log.c \
  tests/libc/test_uchar_c23.c -o /tmp/test_uchar_c23
/tmp/test_uchar_c23 | tee /tmp/test_uchar_c23.log
! grep -q '^SKIP mbrtoc8/c8rtomb:' /tmp/test_uchar_c23.log
grep -q '^uchar_c23: ok' /tmp/test_uchar_c23.log
```

Current output is `SKIP mbrtoc8/c8rtomb: …`, `uchar_c23: ok`
(`tests/libc/test_uchar_c23.c:251,266`).

---

## 6. No fabrication statement

**No constants, lookup tables, rounding values, signatures, or algorithms were
fabricated by this work.** Specifically:

- No C23 API body was written, stubbed, emulated, or copied. The 23 math
  entry points remain declared-only `extern`s in
  `libc/src/math/math_c23_upstream.c`; the four adjacent APIs likewise emit no
  implementation.
- No `FE_TIESAWAY` value was invented; the capability macro stays 0 unless the
  host defines it.
- No `mbrtoc8`/`c8rtomb` code was adapted from the unmerged musl patch; the
  patch text in `docs/libc/UCHARC23_UPSTREAM.md` is reference-only.
- No `timespec_getres` body was derived from `clock_getres`/`timespec_get`.
- No upstream Sollya coefficient, `EXP_HI`/`EXP_MID` table, `ExceptValues`
  entry, or `fputil` primitive was reproduced in this repository.
- The SKIP lines in the existing tests are deliberate truthful reports of the
  blockers above, not failures masked as passes. The `EQUIV … exercised` lines
  in `tests/family_gaps/math_gaps.c` exercise *available* nearest equivalents
  and are explicitly **not** validation of the missing C23 APIs.

---

## 7. Incorrect claims found (reported only — no existing file was edited)

### 7.1 In `docs/libc/C23_MATH_UPSTREAM.md`

1. **"Every one of the 23 upstream implementations is a thin wrapper around a
   C++ template in `libc/src/__support/FPUtil/`"** (lines 25-27) is false.
   - `exp16` is *not* a wrapper — the document itself says so at line 108
     ("full algorithm … the entire algorithm and tables would have to be
     vendored").
   - `hypot` (3-arg) has no upstream implementation at all (line 104).
   - `faddf`, `fsubf`, `fmulf`, `fdivf` have no upstream file at all
     (lines 93, 96, 99, 102).
   The accurate statement is: 17 of the 23 have a wrapper available (6
   nextup/nextdown, 8 non-`/f` fadd/fsub/fmul/fdiv, `frexp16`, `logb16`, and
   `f16fma` as a *non-matching* closest file); 4 have no file; 1 has no
   implementation; and `exp16` is a full algorithm. 17 + 4 + 1 + 1 = 23.

2. **C23 return type for `faddl` is given as `double`** (line 94, "not C23
   `double faddl(long double,long double)`"; line 116, "C23 specifies same-width
   signatures: `double fadd(double,double)`, `double faddl(long double,long
   double)`, …"). C23 actually specifies `long double faddl(long double, long
   double)`; "same-width" contradicts writing `double`. The repo itself uses
   the correct type in `libc/src/math/math_c23.c:209` (`extern long double
   cplus_faddl(long double, long double)`) and in
   `docs/libc/MATH_GAPS_EVIDENCE.md:56`. The upstream-vs-C23 mismatch remains
   real (upstream returns `float`), but the C23 side of the comparison is
   misstated.

3. **"every helper pulled in lives under `libc/src/__support/FPUtil/` or
   `libc/src/__support/CPP/`"** (lines 39-40) is incomplete. `expxf16.h` lives
   under `libc/src/math/generic/`, and `hdr/math_macros.h`,
   `hdr/errno_macros.h`, `hdr/fenv_macros.h` and `macros/*` are compiler-header
   layers outside those two directories — all of which the same document
   cites at lines 86, 98 and 108. This matters for the closure: the required
   import is larger than that sentence implies.

### 7.2 In `docs/libc/iso_c_inventory.json` (not the named document, but affects the count)

For `math.h`, `by_classification` claims `{"HOST/COMPILER-PROVIDED": 23,
"MISSING": 34, "PLATFORM_BACKED": 166}` while the actual `apis[]` entries are
`PLATFORM_BACKED` 174, `HOST/COMPILER-PROVIDED` 23, `IMPLEMENTED_UPSTREAM` 3,
`MISSING` 23 (sum 223 in both cases). The `IMPLEMENTED_UPSTREAM` category is
absent from the metadata entirely. `C23_MATH_UPSTREAM.md`'s claim of "exactly
these 23" is correct; the inventory metadata is the erroneous artifact.

### 7.3 In `docs/libc/MATH_GAPS_EVIDENCE.md` (not the named document)

Lines 67 and 159-161 state that `_Float16` is not available from this host
toolchain and that the four `_Float16` gaps therefore use `double`/`float`
equivalents. This is false: `/usr/bin/clang -std=c99 -dM -E -x c /dev/null`
defines `__FLT16_MANT_DIG__ = 11`, and `libc/src/math/math_c23.c:316` records
the same. The real blocker for those four APIs is the C++ `__support` closure
(and `fma16`'s signature mismatch), not type availability; the nearest-equivalent
`double`/`float` exercises are a testing convenience, not a toolchain necessity.
