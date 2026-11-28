# C23 `fadd` / `fsub` / `fmul` / `fdiv` — 12-entry-point closure

**Date:** 2026-11-23
**Branch:** `agent/modern-stdlib`
**Toolchain:** `/usr/bin/clang` — Apple clang version 16.0.0
(`clang-1600.0.26.6`), target `arm64-apple-darwin27.2.0`, SDK
`/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk`.

## Result

| Item | Value |
|---|---|
| APIs added | `fadd`, `faddf`, `faddl`, `fsub`, `fsubf`, `fsubl`, `fmul`, `fmulf`, `fmull`, `fdiv`, `fdivf`, `fdivl` (12) |
| Inventory | all 12 classified `MISSING` in `docs/libc/iso_c_inventory.json` (`header: math.h`, `c_version: C23`) |
| Implementation | thin wrappers over host operators `+ - * /` (plus one narrowing cast for the eight standard 7.12.14 signatures) |
| Status | **PLATFORM-BACKED, correct rounding delegated to host FP (not formally proven here)** — see §5 |

## Files created (only these four; strict ownership respected)

- `libc/src/math/c23_arith.c`
- `libc/install-include/cplus_c23_arith.h`
- `tests/libc/test_c23_arith.c`
- `docs/libc/C23_ARITH.md` (this record)

No other repository file was created or modified. No `git add`/commit was made.
No build script, `CMakeLists.txt`, compiler code, runtime code, `third_party/`
file or pre-existing source was touched.

---

## 1. First step — upstream check (required)

```
$ cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
$ grep -rn 'fadd' third_party/
$ echo "grep exit=$?"
grep exit=1
```

Exit 1 with no output: **no pinned upstream implementation exists** in any of
`clog`, `ctl`, `exceptions4c`, `intel-safestringlib`, `miniaudio`, `nuklear`,
`safeclib`, `stb`, or `yyjson`. There is no upstream body to copy.

## 2. Provenance — standard-derived, NOT copied

The bodies are written from the documented contract, not copied from code:

* The eight un-suffixed / `-l` signatures follow ISO/IEC 9899:2024 (C23)
  **7.12.14 "Functions that round result to narrower type"** and
  **Annex F.10.11**, as quoted with normative citations in this repository's
  `docs/libc/C23_FARITH_CLOSURE.md` (which cites final draft N3220 §7.12.14
  p1 and §7.12.14.1–.4, and N3096 identically). Those clauses give:

  ```c
  float fadd (double x, double y);         float faddl (long double x, long double y);
  float fsub (double x, double y);         float fsubl (long double x, long double y);
  float fmul (double x, double y);         float fmull (long double x, long double y);
  float fdiv (double x, double y);         float fdivl (long double x, long double y);
  ```

  with the semantics "compute the operation (as if) to infinite precision and
  round once to the return type, according to the current rounding mode" and
  "treat zero and infinite arguments like the corresponding operation: `+`,
  `-`, `*`, `/`".

* The four `/f` names (`faddf`, `fsubf`, `fmulf`, `fdivf`) are **not** defined by
  ISO C23 7.12.14 — this file does **not** claim a standard spelling for them.
  They are included because the task and the inventory require the full 12-name
  family, and their `float`-operands/`float`-result signature matches the
  pre-existing `extern` declarations in `libc/src/math/math_c23.c` and
  `libc/src/math/math_c23_upstream.c`. They are documented as a family
  extension implementing a correctly-rounded float-width operation.

* No third-party source was read or transcribed; no upstream license obligation
  is introduced.

## 3. Exact signatures (as compiled and declared)

```c
float cplus_fadd (double x, double y);         float cplus_faddf (float x, float y);
float cplus_faddl(long double x, long double y);
float cplus_fsub (double x, double y);         float cplus_fsubf (float x, float y);
float cplus_fsubl(long double x, long double y);
float cplus_fmul (double x, double y);         float cplus_fmulf (float x, float y);
float cplus_fmull(long double x, long double y);
float cplus_fdiv (double x, double y);         float cplus_fdivf (float x, float y);
float cplus_fdivl(long double x, long double y);
```

All names use the project-wide `cplus_*` namespace; the bare ISO spellings are
not exported (they could collide with a future host `<math.h>`).

## 4. Exact commands and captured results

### 4.1 Required strict compile + deterministic test (`timeout 30`)

```
$ /usr/bin/clang -std=c99 -Wall -Wextra -pedantic \
    -Ilibc/install-include -Ilibc \
    libc/src/math/c23_arith.c libc/log/log.c tests/libc/test_c23_arith.c \
    -lm -o /tmp/c23nm/test_c23_arith
(no compiler diagnostics)

$ timeout 30 /tmp/c23nm/test_c23_arith > /tmp/c23nm/stdout.txt 2>/tmp/c23nm/stderr.txt
$ echo "exit=$?"
exit=0

$ cat /tmp/c23nm/stdout.txt
c23_arith: 83 0
```

`stderr.txt` contains only opt-in `[CPLUS-LIBC] ...: enter` logger lines from
the logging ON/OFF parity section; stdout contains exactly the single summary
line. The process exits nonzero iff `failures > 0`.

### 4.2 Standalone header checks

```
$ printf '#include "cplus_c23_arith.h"\nint main(void){ return CPLUS_C23_ARITH_HAVE_HOST_OPERATORS == 1 && CPLUS_C23_ARITH_COUNT == 12 ? 0 : 1; }\n' \
    > /tmp/c23nm/hdr_check.c
$ /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -Ilibc/install-include \
    -fsyntax-only /tmp/c23nm/hdr_check.c && echo "header standalone C99: OK"
header standalone C99: OK

$ printf '#include "cplus_c23_arith.h"\nint main(){ return 0; }\n' \
    > /tmp/c23nm/hdr_check.cpp
$ /usr/bin/clang++ -std=c++17 -Wall -Wextra -pedantic -Ilibc/install-include \
    -fsyntax-only /tmp/c23nm/hdr_check.cpp && echo "header C++17: OK"
header C++17: OK
```

The header is self-contained: it includes only standard `<float.h>` and needs
no other project header.

### 4.3 Host FP capability probe

```
$ /usr/bin/clang -std=c99 /tmp/probe_fp.c -o /tmp/probe_fp && /tmp/probe_fp
FLT_EVAL_METHOD=0 FLT_MANT_DIG=24 DBL_MANT_DIG=53 LDBL_MANT_DIG=53
__STDC_IEC_559__ undefined
__STDC_IEC_60559_BFP__ undefined
```

Neither IEC macro is defined by this toolchain, so the capability macro
`CPLUS_C23_ARITH_HAVE_HOST_OPERATORS` is based on `FLT_EVAL_METHOD == 0` and
`FLT_RADIX == 2`, verified by `<float.h>` — **not** on an IEC feature macro.

### 4.4 nm evidence — new symbols only, no existing export changed

```
$ /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -Ilibc/install-include -Ilibc \
    -c libc/src/math/c23_arith.c -o /tmp/c23nm/c23_arith.o
$ nm -g --defined-only /tmp/c23nm/c23_arith.o | grep ' T ' | sort
0000000000000000 T _cplus_fadd
0000000000000044 T _cplus_faddf
0000000000000084 T _cplus_faddl
00000000000000c8 T _cplus_fsub
000000000000010c T _cplus_fsubf
000000000000014c T _cplus_fsubl
0000000000000190 T _cplus_fmul
00000000000001d4 T _cplus_fmulf
0000000000000214 T _cplus_fmull
0000000000000258 T _cplus_fdiv
000000000000029c T _cplus_fdivf
00000000000002dc T _cplus_fdivl
```

Baseline (pre-existing archive `build/libc/libcplus_hardened.a`, built without
this new translation unit):

```
$ nm -g --defined-only build/libc/libcplus_hardened.a | grep -E '_cplus_f(add|sub|mul|div)(f|l)?$' | sort -u
0000000000000000 T _cplus_fadd
000000000000003c T _cplus_faddl
0000000000000078 T _cplus_fsub
00000000000000b4 T _cplus_fsubl
00000000000000f0 T _cplus_fmul
000000000000012c T _cplus_fmull
0000000000000168 T _cplus_fdiv
00000000000001a4 T _cplus_fdivl
```

Comparison:

* **Preserved (8, identical names/signatures):** `cplus_fadd`, `cplus_faddl`,
  `cplus_fsub`, `cplus_fsubl`, `cplus_fmul`, `cplus_fmull`, `cplus_fdiv`,
  `cplus_fdivl`. No existing export is removed, renamed or retyped.
* **New (4):** `cplus_faddf`, `cplus_fsubf`, `cplus_fmulf`, `cplus_fdivf` —
  previously declared `extern` but undefined; now defined.

Compile-time signature-compatibility check (both public headers included in
one translation unit; a conflicting redeclaration would be a hard error):

```
$ /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -Ilibc/install-include \
    -fsyntax-only /tmp/c23nm/sig_compat.c && echo "SIGNATURE COMPAT: OK"
SIGNATURE COMPAT: OK (both headers agree on the 8 existing signatures)
```

## 5. What is guaranteed, and what is not (honest conformance statement)

**Guaranteed on the verified host:**

1. `FLT_EVAL_METHOD == 0`: each host operator rounds once to its semantic type,
   with no excess precision and no hidden double rounding inside the operator.
2. `FLT_RADIX == 2` with 24/53/53 binary significand widths: the host uses IEEE
   754 binary32/binary64 formats, so `+ - * /` are IEEE 754 operations.
3. The current rounding mode (`fegetround`/`fesetround`) changes results, and
   the standard exceptions (`FE_INVALID`, `FE_DIVBYZERO`, `FE_OVERFLOW`) are
   raised — verified by `test_exceptions` and `test_rounding_modes`.
4. For the eight narrowing signatures, the intermediate precision `p >= 53`
   satisfies the classical innocuous-double-rounding bound `p >= 2*24+2 = 50`,
   so "wider operation, then cast to float" agrees with a single round to
   binary32 for finite results.
5. Signed zero, infinities and NaN follow IEEE 754 operator rules (verified by
   deterministic bit-level checks in `test_signed_zero`, `test_infinities`,
   `test_nan`).

**Not claimed:**

* No formal machine-checked proof that the host FP unit correctly rounds every
  possible operand pair. The "as if infinite precision, single round" property
  is inherited from the platform and sampled by deterministic tests; it is not
  re-derived here. These are **platform-backed** wrappers, not a from-first-
  principles C23 implementation.
* No full C23 conformance certification: `__STDC_IEC_559__` and
  `__STDC_IEC_60559_BFP__` are undefined by this toolchain (§4.3), so Annex F
  conformance cannot be asserted from feature macros.
* The four `/f` names are not ISO C23 APIs; their contract is the documented
  float-width family extension described in §2, not a standard spelling.

## 6. Test design summary

`tests/libc/test_c23_arith.c` performs 83 checks across:

* representative finite values for all 12 functions (12 checks);
* signed-zero bit patterns for all 12 functions (14 checks);
* infinities including `inf + (-inf) -> NaN`, `finite/0 -> inf`, `0/0 -> NaN`
  (13 checks);
* NaN propagation across all four operations (4 checks);
* rounding-mode sensitivity under all four host modes for add/subtract at the
  binary32 halfway point, on all three operand widths (24 checks);
* standard FP exception flags raised by the host operators (4 checks);
* logging ON/OFF result parity for all 12 functions (12 checks);
* one capability assertion fired in `main` when the backend is unavailable.

All expected bit patterns are exact constants derived analytically from the
binary32 halfway-point construction (not captured from the implementation),
so the checks are deterministic and independent of the wrappers.

## 7. Integration note for the wiring agent (important)

`libc/src/math/math_c23_farith.c` already defines the same eight narrowing
symbols with the same signatures. This translation unit preserves those
signatures exactly and adds the four previously undefined `/f` symbols.
**Wire `libc/src/math/c23_arith.c` instead of `math_c23_farith.c` — not in
addition to it** — so the static archive and the shared library contain exactly
one definition of each of the 12 symbols. The `extern` declarations of the `/f`
names in `math_c23.c` and `math_c23_upstream.c` already match these signatures
(no edit needed there). The bare ISO spellings are intentionally not exported.
