# C23 `fadd` / `fsub` / `fmul` / `fdiv` family closure

**Date:** 2026-11-23
**Branch:** `agent/modern-stdlib`
**Toolchain:** `/usr/bin/clang` — Apple clang version 16.0.0
(`clang-1600.0.26.6`), target `arm64-apple-darwin27.2.0`, SDK
`/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk`.

## Result

| Status | APIs | Count |
|---|---|---:|
| **CLOSED** (host operator semantics) | `fadd`, `faddl`, `fsub`, `fsubl`, `fmul`, `fmull`, `fdiv`, `fdivl` | 8 |
| **BLOCKED** (not C23 APIs) | `faddf`, `fsubf`, `fmulf`, `fdivf` | 4 |

The eight CLOSED entry points have real, dependency-closed definitions that
delegate to documented host IEEE 754 operators plus one narrowing cast. The
four BLOCKED names are not ISO C23 APIs at all; no symbol or declaration is
emitted for them.

## Files created (only these four; strict ownership respected)

- `libc/src/math/math_c23_farith.c` — eight wrapper definitions.
- `libc/install-include/cplus_math_c23_farith.h` — public declarations,
  capability macro and machine-readable blocker strings.
- `tests/libc/test_math_c23_farith.c` — standalone C99 test (194 checks).
- `docs/libc/C23_FARITH_CLOSURE.md` — this record.

No other repository file was created or modified; no `git add`/commit was made;
no compiler, runtime, `CMakeLists.txt`, build script or existing source was
touched.

---

## 1. Normative C23 contract (decisive new evidence)

The earlier audit `docs/libc/C23_MATH_DEPENDENCY_CLOSURE.md` (rows 7–18,
§7.1 item 2) classified all 12 names as blocked, citing a *signature mismatch*
between LLVM libc (`float fadd(double,double)`) and an alleged C23
`double fadd(double,double)`. **That signature claim is wrong**, and the wrong
claim was the main reason these were thought unimplementable. The actual C23
text is:

> **7.12.14 Functions that round result to narrower type**
> "The functions in this subclause round their results to the return type,
> which is typically narrower than the parameter types."
>
> **7.12.14.1 Add and round to narrower type**
> `float fadd (double x, double y);`
> `float faddl (long double x, long double y);`
> "These functions compute the sum of x + y, rounded to the return type of the
> function. They compute the sum (as if) to infinite precision and round once
> to the return type, according to the current rounding mode. A range error
> occurs for some finite arguments. A domain error may occur for infinite
> arguments."
>
> 7.12.14.2/.3/.4 give the identical shape for `fsub`/`fsubl`, `fmul`/`fmull`
> and `fdiv`/`fdivl`, all **returning `float`**; 7.12.14.4 additionally states
> "A pole error occurs for a finite x and a zero y."

Sources (fetched 2026-11-23):

- Final C23 working draft **N3220**, ISO/IEC 9899:2024, 2024-02-21,
  §7.12.14 p1 and §7.12.14.1–.4:
  `https://www.open-std.org/jtc1/sc22/wg14/www/docs/n3220.pdf`
- Earlier draft **N3096**, ISO/IEC 9899:2023, April 2023, §7.12.14 p1 and
  §7.12.14.1–.4:
  `https://www.open-std.org/jtc1/sc22/wg14/www/docs/n3096.pdf` — identical
  signatures and wording.
- Annex F (normative), **F.10.11**: "The functions that round their result to
  narrower type (7.12.14) are fully specified in IEC 60559. The returned value
  is dependent on the current rounding direction mode. These functions treat
  zero and infinite arguments like the corresponding operation: `+`, `-`, `*`,
  `/`, `fma`, or `sqrt`."
- Annex **F.3 p13** explains why the family exists: IEC 60559 requires
  operations that round to same/wider/narrower formats, and "getting a result
  in a given format might require a cast that could introduce an extra
  rounding error" when evaluation formats are wider than the semantic type.

**Consequences:**

1. LLVM libc's `float fadd(double,double)` and `float faddl(long double,long
   double)` signatures at `llvmorg-20.1.1` **do match C23**. The prior
   "signature mismatch" blocker is invalid; the real blocker was only the
   unvendored C++ `__support` closure.
2. **There is no `faddf`, `fsubf`, `fmulf` or `fdivf` in C23.** §7.12.14 lists
   exactly `fadd/faddl/daddl`, `fsub/fsubl/dsubl`, `fmul/fmull/dmull`,
   `fdiv/fdivl/ddivl` (plus DFP variants). This also explains the earlier
   finding that "4 of these have no upstream file" (`faddf.cpp` etc. are
   HTTP 404 at the pin): there is no standard contract for them to implement.
3. C23 `fadd(x,y)` is *literally* "`x + y` computed to infinite precision and
   rounded once to `float`". This is the contract the task calls "host
   operator semantics", now documented with primary-source evidence rather
   than asserted.

---

## 2. Host probes

All probes below were run with the system toolchain from the repository root.

### 2.1 No builtin, no host header declaration

```
$ /usr/bin/clang --version
Apple clang version 16.0.0 (clang-1600.0.26.6)
Target: arm64-apple-darwin27.2.0

$ # __has_builtin probe (excerpt)
__builtin_fadd = 0        __builtin_faddf = 0       __builtin_faddl = 0
__builtin_fsub = 0        __builtin_fsubf = 0       __builtin_fsubl = 0
__builtin_fmul = 0        __builtin_fmulf = 0       __builtin_fmull = 0
__builtin_fdiv = 0        __builtin_fdivf = 0       __builtin_fdivl = 0
__builtin_add  = 0        __builtin_faddf16 = 0

$ grep -nE '\bf(add|sub|mul|div)(f|l)?\s*\(' \
    /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include/math.h
# (no matches)
```

So a builtin-wrapper route is unavailable; the host-operator route is the
only dependency-closed one.

### 2.2 FP environment: single rounding is guaranteed

```
$ /usr/bin/clang -std=c99 -Wall -Wextra -pedantic /tmp/fpformat.c -o /tmp/fpformat && /tmp/fpformat
FLT_EVAL_METHOD=0 DBL_MANT_DIG=53 FLT_MANT_DIG=24 LDBL_MANT_DIG=53 sizeof(ld)=8
p_mid_double=%d 2*p_float+2=50 -> innocuous=1
p_mid_ldouble=%d 2*p_float+2=50 -> innocuous=1
```

`FLT_EVAL_METHOD == 0` means the host evaluates `x + y`, `x - y`, `x * y` and
`x / y` in the semantic type with one IEEE 754 rounding (no excess precision).
On this arm64/Darwin target `long double` is the same 64-bit binary format as
`double` (`LDBL_MANT_DIG=53`, `sizeof=8`), so both operand families have
`p_mid = 53`.

**Innocuous double rounding.** With intermediate precision `p_mid >= 2*p + 2`
(here `53 >= 2*24 + 2 = 50`), rounding the exact result to the intermediate
format and then narrowing to binary32 is provably identical to a single
correct rounding directly to binary32. This is the classical
round-twice-is-once theorem for IEEE binary formats (round-to-nearest and the
directed modes). Hence `(float)(x op y)` satisfies C23 §7.12.14's "(as if) to
infinite precision and round once to the return type". This is *not* a
fabricated algorithm: the implementation consists only of host operators and
the standard cast, and the theorem establishes their equivalence to the C23
contract.

Empirical cross-check over ~2M random finite doubles (temporary probe outside
the repo): `(float)(x+y)` versus a wider reference and `(float)(x*y)` versus an
exact 106-bit product both produced **0 mismatches**.

### 2.3 Rounding modes are honoured

```
$ /usr/bin/clang -std=c99 -Wall -Wextra -pedantic /tmp/verify_rounding.c -lm -o /tmp/verify_rounding && /tmp/verify_rounding
nearest     1+2^-25 -> 3f800000  1-2^-25 -> 3f800000
upward      1+2^-25 -> 3f800001  1-2^-25 -> 3f800000
downward    1+2^-25 -> 3f800000  1-2^-25 -> 3f7fffff
towardzero  1+2^-25 -> 3f800000  1-2^-25 -> 3f7fffff
nearest     tie(1+2^-24) -> 3f800000
upward      tie(1+2^-24) -> 3f800001
downward    tie(1+2^-24) -> 3f800000
towardzero  tie(1+2^-24) -> 3f800000
```

This matches C23 §7.12.14 "according to the current rounding mode" and Annex
F.10.11's "dependent on the current rounding direction mode".

### 2.4 Exact-rational oracle (not a self-comparison)

An independent Python generator (`/tmp/farith_vectors.py`, reproduced in
spirit by the table embedded in the test) computed 76 expected binary32 bit
patterns using `fractions.Fraction` for `x+y`, `x-y`, `x*y`, `x/y` and a
hand-written single round-to-nearest-even into binary32. No host floating-point
operation was used in the oracle. The cases include:

- binary32 midpoints at 1.0 (tie-to-even) and just-above/just-below midpoints;
- `FLT_MAX + 1` and products/quotients that overflow to infinity;
- the smallest subnormal `2^-149`, half of it (`2^-150`, tie-to-even → 0), and
  quarter of it;
- `1/3`, `1/7`, `FLT_MAX/(1+2^-24)`, `2^120/2^-200` and similar boundaries;
- 48 deterministic random finite pairs (seed 20261123).

Sanity check of the oracle itself:
`Fraction(1) + Fraction(2)**-24` rounds to `0x3f800000`, matching
`struct.pack('<f', 1.0 + 2.0**-24)`.

A preliminary C harness verified the formula against all 76 vectors:

```
$ /usr/bin/clang -std=c99 -Wall -Wextra -pedantic /tmp/verify_formula.c -lm -o /tmp/verify_formula && /tmp/verify_formula
verify_formula: vectors=76 failures=0
```

---

## 3. Implementation

`libc/src/math/math_c23_farith.c` defines exactly these eight symbols, guarded
by `CPLUS_C23_FARITH_HAVE_OPERATOR_BACKEND` (1 on this host, computed from
`<float.h>` in the public header):

```c
float cplus_fadd (double x, double y)              { ... return (float)(x + y); }
float cplus_faddl (long double x, long double y)   { ... return (float)(x + y); }
float cplus_fsub (double x, double y)              { ... return (float)(x - y); }
float cplus_fsubl (long double x, long double y)   { ... return (float)(x - y); }
float cplus_fmul (double x, double y)              { ... return (float)(x * y); }
float cplus_fmull (long double x, long double y)   { ... return (float)(x * y); }
float cplus_fdiv (double x, double y)              { ... return (float)(x / y); }
float cplus_fdivl (long double x, long double y)   { ... return (float)(x / y); }
```

Each function emits the conventional `cplus_libc_log("<api>", "enter")`
marker. If the host ever reports `FLT_EVAL_METHOD != 0` or an intermediate
format narrower than `2*24+2`, the macro becomes 0, **no definition is emitted**
and the header publishes no declarations — a truthful platform gap rather than
a wrong `a+b`.

### Why no upstream file was copied

The pinned LLVM libc sources for these APIs (`generic/fadd.cpp`, `faddl.cpp`,
`fsub.cpp`, `fsubl.cpp`, `fmul.cpp`, `fmull.cpp`, `fdiv.cpp`, `fdivl.cpp` at
`llvmorg-20.1.1` / commit `424c2d9b7e4de40d0804dd374721e6411c27d1d1`) depend
on the unvendored C++ `__support/FPUtil/generic/{add_sub,mul,div}.h` templates
(`DyadicFloat`, `FPBits`, `quick_mul`, …). Nothing was copied: the host
operator mechanism is self-contained, requires no C++ translation unit and
needs no new upstream import.

---

## 4. The four BLOCKED names (exact reasons)

`faddf`, `fsubf`, `fmulf`, `fdivf` are recorded in the older inventory, but
**ISO/IEC 9899:2024 §7.12.14 does not define them** — it specifies only
`fadd/faddl`, `fsub/fsubl`, `fmul/fmull`, `fdiv/fdivl` (plus `d*-prefixed`
double-result and decimal variants). There is therefore no C23 contract, no
host builtin/symbol, and no pinned upstream file (the earlier audit verified
`faddf.cpp`, `fsubf.cpp`, `fmulf.cpp`, `fdivf.cpp` are HTTP 404 at the pin).
Implementing any of them as `float f(x,y){return x op y;}` would be inventing a
non-standard API and claiming C23 parity for it — explicitly disallowed. The
exact blocker strings are published as `CPLUS_C23_FARITH_BLOCKER_*` macros in
the header and printed verbatim as `SKIP` lines by the test.

Unblock condition: a normative document defining these names (or an explicit
orchestrator decision to expose a documented non-standard extension).

---

## 5. Exact verification commands and results

All commands were run from
`/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib`.

### 5.1 Direct compile, link and run (no archive)

```sh
SYS=/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk
/usr/bin/clang -std=c99 -Wall -Wextra -pedantic -isysroot "$SYS" \
  -Ilibc -Ilibc/include -Ilibc/install-include \
  libc/src/math/math_c23_farith.c libc/log/log.c \
  tests/libc/test_math_c23_farith.c -lm -pthread -o /tmp/test_math_c23_farith
/tmp/test_math_c23_farith
```

Output (stderr logging markers from the parity check omitted here):

```
test_math_c23_farith: SKIP faddf (faddf: not an ISO C23 API -- 7.12.14 defines only fadd(double,double) and faddl(long double,long double), both returning float; ...)
test_math_c23_farith: SKIP fsubf (fsubf: not an ISO C23 API -- 7.12.14 has no /f variant; ...)
test_math_c23_farith: SKIP fmulf (fmulf: not an ISO C23 API -- 7.12.14 has no /f variant; ...)
test_math_c23_farith: SKIP fdivf (fdivf: not an ISO C23 API -- 7.12.14 has no /f variant; ...)
test_math_c23_farith: checks=194 failures=0 skipped=4
```

**PASS — 194 checks, 0 failures, 0 compiler warnings, 4 intentional SKIPs.**
The checks comprise 152 exact-oracle comparisons (76 vectors × double and
long-double wrappers), 24 special-value/classification checks, 12 rounding-mode
checks, 5 fenv-exception checks and 1 logging-parity check.

### 5.2 UBSan

```sh
/usr/bin/clang -std=c99 -Wall -Wextra -pedantic \
  -fsanitize=undefined -fno-sanitize-recover=all -isysroot "$SYS" \
  -Ilibc -Ilibc/include -Ilibc/install-include \
  libc/src/math/math_c23_farith.c libc/log/log.c \
  tests/libc/test_math_c23_farith.c -lm -pthread -o /tmp/test_math_c23_farith_ubsan
UBSAN_OPTIONS=print_stacktrace=1 /tmp/test_math_c23_farith_ubsan
# -> ... checks=194 failures=0 skipped=4 ; no UBSan diagnostics
```

### 5.3 Archive integration (no duplicate symbols with existing TUs)

```sh
cp build/libc/libcplus_hardened.a /tmp/libc_with_farith.a
/usr/bin/clang -std=c99 -Wall -Wextra -pedantic -isysroot "$SYS" \
  -Ilibc -Ilibc/include -Ilibc/install-include \
  -c libc/src/math/math_c23_farith.c -o /tmp/math_c23_farith.o
ar rcs /tmp/libc_with_farith.a /tmp/math_c23_farith.o
/usr/bin/clang -std=c99 -Wall -Wextra -pedantic -isysroot "$SYS" \
  -Ilibc -Ilibc/include -Ilibc/install-include \
  tests/libc/test_math_c23_farith.c /tmp/libc_with_farith.a -lm -pthread \
  -o /tmp/test_farith_archive
/tmp/test_farith_archive | tail -1
# test_math_c23_farith: checks=194 failures=0 skipped=4

nm -g /tmp/math_c23_farith.o | grep ' T '
# 0000000000000000 T _cplus_fadd
# 0000000000000044 T _cplus_faddl
# 0000000000000198 T _cplus_fdiv
# 00000000000001dc T _cplus_fdivl
# 0000000000000110 T _cplus_fmul
# 0000000000000154 T _cplus_fmull
# 0000000000000088 T _cplus_fsub
# 00000000000000cc T _cplus_fsubl
```

### 5.4 Header smoke test (C and C++)

```sh
/usr/bin/clang -std=c99 -Wall -Wextra -pedantic -isysroot "$SYS" \
  -Ilibc/install-include -fsyntax-only /tmp/farith_hdr.c     # OK
/usr/bin/clang++ -std=c++17 -Wall -Wextra -pedantic -isysroot "$SYS" \
  -Ilibc/install-include -fsyntax-only /tmp/farith_hdr.cpp   # OK
```

---

## 6. Integration requirements for the orchestrator

These files are additive and were not wired into the build (strict ownership):

1. Add `src/math/math_c23_farith.c` to the libc source list
   (`libc/CMakeLists.txt`) and to the loop in `tests/libc/build_libc.sh`.
2. **Fix the stale declarations in `libc/src/math/math_c23.c`** and
   `libc/src/math/math_c23_upstream.c`. They currently declare
   `extern double cplus_fadd(double,double);`, `extern long double
   cplus_faddl(long double,long double);` etc. — the wrong (non-C23)
   signatures. They emit no symbol, so there is no link conflict today, but a
   consumer including both that surface and `cplus_math_c23_farith.h` would see
   incompatible declarations. Replace them with the `float`-returning
   signatures from §3 or remove them in favour of the new header. Similarly,
   the declared-but-undefined `cplus_faddf/fsubf/fmulf/fdivf` should be removed
   because they are not C23 APIs.
3. Optionally forward-declare the eight names from `cplus_libc.h` or include
   the new header there.

## 7. Deviations / limitations (explicit)

1. **Not a from-scratch bit-level C23 implementation.** The bodies are thin
   delegations to host operators. They are value-equivalent to C23 §7.12.14 on
   this host, proven by the innocuous-double-rounding condition and the exact
   oracle, but they are `PLATFORM_BACKED` wrappers.
2. **Portability is guarded, not universal.** On a host with
   `FLT_EVAL_METHOD != 0` or an intermediate format below `2*24+2` bits the
   backend macro becomes 0 and no symbol is emitted; such a host needs the LLVM
   `__support` C++ closure route instead.
3. **Bare ISO names are not exported** — only `cplus_*` names, matching the
   project convention.
4. **Fenv flags are host-defined and inherited**, not independently
   controlled (e.g. `FE_DIVBYZERO` for `fdiv(x,0)`, `FE_INVALID` for
   `0*inf`, `FE_OVERFLOW` on narrowing overflow). Annex F.10.11 says these
   operations treat such arguments like the corresponding operator, which is
   exactly what the wrappers do.
5. **`long double` on this target is 64-bit binary (`LDBL_MANT_DIG=53`,
   `sizeof=8`)**, so the `/l` wrappers are ABI-distinct symbols with the same
   numeric behaviour as the unsuffixed ones here. The header guard still
   checks the wider-format condition so the work remains correct on targets
   where `long double` is 80/128-bit.

---

## 8. Build integration (integration agent, 2026-11-23)

The eight farith entry points and the five hypot3/_Float16 entry points were
wired into the standard libc build. Files changed (integration scope only):

| File | Change |
|---|---|
| `libc/CMakeLists.txt` | Added `src/math/math_c23_farith.c` and `src/math/math_c23_hypot3_f16.c` to `LIBC_SOURCES` (both the static and shared targets consume this list). |
| `tests/libc/build_libc.sh` | Added the same two translation units to the source loop that produces `build/libc/libcplus_hardened.a`. |
| `libc/src/math/math_c23.c` | Removed the eight stale `extern double/long double cplus_f{add,sub,mul,div}{,l}(...)` declarations (wrong non-C23 signatures) from the `#else` arms of the builtin guards; the branches now point to `math_c23_farith.c`. The `cplus_faddf/fsubf/fmulf/fdivf` externs were intentionally left untouched: the new translation unit does not define those symbols, so they are outside the "duplicate/conflict with the new definitions" removal scope. |
| `libc/src/math/math_c23_upstream.c` | Removed the same eight wrong-signature farith externs (plus the hypot3 and four `_Float16` externs, now defined in `math_c23_hypot3_f16.c`); the four `/f` externs remain declared-only. |
| `libc/install-include/cplus_libc.h` | Additively included `cplus_math_c23_farith.h` and `cplus_math_c23_hypot3_f16.h` so the public surface exposes the new declarations. |
| `docs/libc/C23_FARITH_CLOSURE.md`, `docs/libc/C23_HYPOT_F16_CLOSURE.md` | Added this integration record. |

Note: `math_c23.c` still contains builtin-guarded *definitions*
(`#if CPLUS_C23_HAS_BUILTIN(__builtin_fadd)` → `double cplus_fadd(...)`). On
this host those builtins are absent (has_builtin=0), so the branches are
inactive and there is no duplicate definition. On a hypothetical host that
exposes `double`-returning `__builtin_fadd` et al., those definitions would
collide with the `float`-returning ones in `math_c23_farith.c`; that is a
cross-portability concern outside this host's verification, not a current
blocker (see "Remaining blockers" below).

### Verification (exact, `CPLUS_CLANG=/usr/bin/clang`)

```
$ CPLUS_CLANG=/usr/bin/clang sh tests/libc/build_libc.sh
/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/libc/libcplus_hardened.a

$ nm -g build/libc/libcplus_hardened.a | grep ' T _cplus_'  # excerpt
0000000000000000 T _cplus_fadd
000000000000003c T _cplus_faddl
0000000000000078 T _cplus_fsub
00000000000000b4 T _cplus_fsubl
00000000000000f0 T _cplus_fmul
000000000000012c T _cplus_fmull
0000000000000168 T _cplus_fdiv
00000000000001a4 T _cplus_fdivl

$ CPLUS_CLANG=/usr/bin/clang sh tests/libc/run_libc_tests.sh
libc tests: 90 passed, 0 failed
ok test_math_c23_farith.c
ok test_math_c23_hypot3_f16.c
```

A first run of the suite reported 4 transient failures
(`test_coverage_untested`, `test_libc_full`, `test_resource_lifecycle`,
`test_stdio_extra`) while other parallel agents were rebuilding the shared
`build/libc/` archive; all four passed when run individually and on a clean
re-run. No source error was involved.

Direct link (source + log + test, no archive), exit 0, no warnings:

```
$ /usr/bin/clang -std=c99 -Wall -Wextra -pedantic \
    -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk \
    -Ilibc -Ilibc/include -Ilibc/install-include \
    libc/src/math/math_c23_farith.c libc/log/log.c \
    tests/libc/test_math_c23_farith.c -lm -pthread -o /tmp/test_math_c23_farith
$ /tmp/test_math_c23_farith | tail -1
test_math_c23_farith: checks=194 failures=0 skipped=4
```

CMake source-list validation (isolated build dir, static target):

```
$ cmake -S libc -B <tmp> -DCMAKE_C_COMPILER=/usr/bin/clang
$ cmake --build <tmp> --target cplus_hardened_libc --parallel 4
[ 84%] Building C object CMakeFiles/.../src/math/math_c23_farith.c.o
[ 86%] Building C object CMakeFiles/.../src/math/math_c23_hypot3_f16.c.o
[100%] Built target cplus_hardened_libc
```

### Remaining blockers

None on this host. One cross-portability caveat (not a verified failure): a
host exposing `double`-returning `__builtin_fadd`/`fsub`/`fmul`/`fdiv`
builtins would make `math_c23.c`'s guarded definitions duplicate the
`float`-returning definitions in `math_c23_farith.c`. Those guards were left
in place because removing them is outside the "extern declarations only"
scope of this integration; resolving it requires a follow-up decision.
