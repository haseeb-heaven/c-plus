# C23 `FE_TIESAWAY`: honest capability surface

Date: 2026-11-23  
Branch: `agent/modern-stdlib`  
Status: **resolved as UNSUPPORTED-BY-HOST** — an explicit, testable refusal, not
a fabricated implementation.

## 1. Classification change (the point of this work)

`docs/libc/COVERAGE_100_AUDIT.md` lists `FE_TIESAWAY` among the 27 **MISSING**
C23 entries and states that it "requires compiler `<fenv.h>` support the host
does not provide; fabricating a value would be false semantics."

This change converts that silent gap into a positive, machine-readable fact:

| | Before | After |
|---|---|---|
| Classification | MISSING (silent gap, counted among 27) | **UNSUPPORTED-BY-HOST** (explicit, testable refusal) |
| Query surface | none | `cplus_fenv_has_tiesaway()` → `0` (false) |
| Status macro | none | `CPLUS_C23_FENV_TIESAWAY_STATUS == "UNSUPPORTED-BY-HOST"` |
| `FE_TIESAWAY` value | absent | still **absent** — no value invented |
| Test evidence | static prose in audit docs | live re-probe + counted checks: `c23_fenv: 8 0` |

Scope note: `docs/libc/COVERAGE_100_AUDIT.md` was **not** edited (file
ownership for this task is limited to the three artifacts listed in §5). That
audit should be updated separately to read `UNSUPPORTED-BY-HOST` for
`FE_TIESAWAY` and to point at this document. Until then this document is the
authoritative reclassification.

Nothing here claims the C23 rounding mode is implemented. The claim is narrower
and verifiable: *the host lacks it, the library says so, and the test proves
both the lack and the report.*

## 2. Probe 1 (required): `<fenv.h>` + `FE_TIESAWAY` under `/usr/bin/clang -std=c11`

Command (stdin source, `-fsyntax-only`, so no file is created):

```sh
printf '#include <fenv.h>\nint main(void) { return FE_TIESAWAY; }\n' | \
  /usr/bin/clang -x c -std=c11 -fsyntax-only - 2>&1; echo "EXIT=$?"
```

Exact output:

```
<stdin>:2:25: error: use of undeclared identifier 'FE_TIESAWAY'
    2 | int main(void) { return FE_TIESAWAY; }
      |                         ^
1 error generated.
EXIT=1
```

Toolchain identity:

```
$ /usr/bin/clang --version
Apple clang version 16.0.0 (clang-1600.0.26.6)
Target: arm64-apple-darwin27.2.0
$ uname -a
Darwin ... 27.2.0 Darwin Kernel Version 27.2.0 ... arm64
```

Include search (`printf '#include <fenv.h>\n' | /usr/bin/clang -x c -std=c11 -E -v -`) resolved to:

```
/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include
```

## 3. Probe 2: no host-provided value anywhere else

```sh
grep -n "FE_TIESAWAY\|FE_TONEAREST\|FE_UPWARD\|FE_DOWNWARD\|FE_TOWARDZERO" \
  /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include/float.h
```
Result: **no `FE_*` matches at all** in `float.h`.

```sh
grep -n "FE_TIESAWAY\|define FE_" .../MacOSX.sdk/usr/include/fenv.h
```
Result: exception macros plus exactly four rounding modes —
`FE_TONEAREST`, `FE_UPWARD`, `FE_DOWNWARD`, `FE_TOWARDZERO`. **No
`FE_TIESAWAY`.** (The header documents the same four in its API comment.)

```sh
grep -rln "FE_TIESAWAY" .../MacOSX.sdk/usr/include
```
Result: **no files** (SDK-wide search).

```sh
grep -rn "FE_TIESAWAY" third_party/
```
Result: **no matches** in the repository's vendored dependencies.

The only in-tree references are prior honest BLOCKED reports:
`libc/install-include/cplus_c23_remainders.h`
(`CPLUS_C23_REMAINDERS_HAVE_FE_TIESAWAY 0`),
`libc/src/c23_small_gaps.c`, and `libc/src/c23_upstream_small.c`. This new
artifacts set agrees with them and adds the missing *testable query*.

## 4. Why no value is fabricated

`FE_TIESAWAY` is a compiler-provided macro, not a function or a data symbol.
There is no runtime symbol to bind and no numeric value to derive: the four
existing mode encodings are host facts (here `0, 0x400000, 0x800000, 0xC00000`),
but the encoding of a fifth mode is a decision only the compiler can make.
Picking bits ourselves could silently select the wrong FP control-register
state. Therefore the header emits **no** `FE_TIESAWAY` substitute; it reports
the capability as false and says why.

Forward compatibility is built in: detection is `#if defined(FE_TIESAWAY)` on
the compiler header. On a future host that defines it, the query returns `1`,
`CPLUS_FE_TIESAWAY` aliases the **real** macro, the status becomes
`SUPPORTED-BY-HOST`, and the test switches to a wrapper-equality assertion.
No fabricated value is introduced in either branch.

## 5. Delivered artifacts (only these three files were created)

1. `libc/install-include/cplus_c23_fenv_compat.h` — header-only, additive:
   - `cplus_fenv_has_tiesaway()` — `static inline int`, returns `0` here.
   - `CPLUS_C23_FENV_HAVE_FE_TIESAWAY` — `0` here, `1` where the host has the macro.
   - `CPLUS_C23_FENV_TIESAWAY_STATUS` — `"UNSUPPORTED-BY-HOST"` here.
   - `CPLUS_C23_FENV_BLOCKER_FE_TIESAWAY` — single authoritative blocker string
     carrying the exact probe diagnostic.
   - `CPLUS_FE_TIESAWAY` — defined **only** as an alias of the real host macro
     when one exists; deliberately undefined on this host.
   - No public symbol, no type, no object-file dependency; C99…C2x and C++ safe.

2. `tests/libc/test_c23_fenv_compat.c` — counted checks, no early abort:
   - asserts the documented status/blocker;
   - prints the recorded diagnostic verbatim **and** re-runs the exact probe
     live via `popen` (`CPLUS_C23_FENV_CLANG` overrides the default
     `/usr/bin/clang`);
   - checks the query returns `0`, the probe still fails to compile with the
     recorded diagnostic and a nonzero exit, and the four host modes remain
     distinct and round-trip through `fesetround`/`fegetround` (positive
     control proving the toolchain is healthy);
   - enforces a wall-clock 30-second budget;
   - prints a single summary line `c23_fenv: <checks> <failures>` and exits
     nonzero iff failures > 0.

3. This document.

## 6. Test commands and exact results

Standalone (from `tests/libc`):

```sh
/usr/bin/clang -O2 -Wall -Wextra -pedantic -std=c99 \
  -I../../libc/install-include test_c23_fenv_compat.c -o test_c23_fenv_compat
./test_c23_fenv_compat
```

Compiler emitted **no diagnostics** (exit 0). Program output (abridged to the
evidence lines and summary; full output is reproducible with the command above):

```
classification: UNSUPPORTED-BY-HOST
blocker: FE_TIESAWAY: UNSUPPORTED-BY-HOST; <fenv.h> defines only FE_TONEAREST/FE_UPWARD/FE_DOWNWARD/FE_TOWARDZERO (probe: use of undeclared identifier 'FE_TIESAWAY'); no numeric value fabricated
recorded probe output (verbatim):
<stdin>:2:25: error: use of undeclared identifier 'FE_TIESAWAY'
1 error generated.
live probe output (verbatim):
<stdin>:2:25: error: use of undeclared identifier 'FE_TIESAWAY'
    2 | int main(void) { return FE_TIESAWAY; }
      |                         ^
1 error generated.
PASS live probe reports: use of undeclared identifier 'FE_TIESAWAY'
PASS live probe exits nonzero (compile failure)
PASS cplus_fenv_has_tiesaway() reports 0 (false)
PASS host exposes four distinct rounding modes
PASS host modes round-trip via fesetround/fegetround
host fenv evidence: modes 0,4194304,8388608,12582912 (FE_TIESAWAY absent)
PASS completed within the 30-second budget
c23_fenv: 8 0
```

Program exit status: **0** (8 checks, 0 failures).

Literal 30-second guard verification:

```sh
/usr/bin/timeout 30 ./test_c23_fenv_compat
# timeout30_exit=0
# last line: c23_fenv: 8 0
```

Also verified under the standard runner's compile line from the repository
root (with the runner's `CPLUS_CLANG` override pointed at the working compiler):

```sh
CPLUS_CLANG=/usr/bin/clang ... \
  /usr/bin/clang -O2 -Wall -Wextra -pedantic -std=c99 \
  -Ilibc -Ilibc/include -Ilibc/install-include \
  tests/libc/test_c23_fenv_compat.c build/libc/libcplus_hardened.a \
  -lm -pthread -o test_c23_fenv_compat
# -> c23_fenv: 8 0, exit 0
```

Environment caveat (pre-existing, unrelated to this test): the default `clang`
on `PATH` is Homebrew clang 23.1.6 (`/opt/homebrew/opt/llvm/bin/clang`) and its
SDK sysroot is broken here (`no such sysroot directory: ... MacOSX27.sdk`,
`'fenv.h' file not found`). `/usr/bin/clang` (Apple clang 16.0.0) is the
healthy compiler used for every command above. The unmodified runner defaults
to `clang` from `PATH`, so use `CPLUS_CLANG=/usr/bin/clang` when running it in
this environment.

## 7. Unblock conditions

The classification may change to `SUPPORTED-BY-HOST` only when **all** hold:

1. the host SDK/compiler `<fenv.h>` defines `FE_TIESAWAY`;
2. `fesetround(FE_TIESAWAY)` / `fegetround()` are verified against it on that
   host (not merely a macro that compiles);
3. the test is re-run there and its future-host branch passes.

Until then the honest answer is the query returning `0` and the string
`UNSUPPORTED-BY-HOST` — never an invented rounding-mode constant.
