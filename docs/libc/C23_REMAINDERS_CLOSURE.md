# C23_REMAINDERS_CLOSURE — the last four C23 interfaces

Scope: `timespec_getres` (`<time.h>`), `mbrtoc8` and `c8rtomb` (`<uchar.h>`),
and `FE_TIESAWAY` (`<fenv.h>`). These are the four C23 entries not handled by
the existing gap files (`libc/src/c23_small_gaps.c`,
`libc/src/c23_upstream_small.c`, `libc/src/uchar/uchar_c23.c`).

Per the task's strict file ownership, only these four new files were created:

| File | Role |
| --- | --- |
| `libc/src/c23/remainders.c` | Probe record; emits no symbol and no fabricated value |
| `libc/install-include/cplus_c23_remainders.h` | Authoritative capability macros + exact blocker strings; additive, macros only |
| `tests/libc/test_c23_remainders.c` | Compile-time status assertions, exact SKIP lines, adjacent host boundary checks |
| `docs/libc/C23_REMAINDERS_CLOSURE.md` | This document |

No build file, compiler/runtime file, third-party file or existing source was
modified. The new `.c` files are **not** wired into `libc/CMakeLists.txt`
(outside this task's ownership); verification used direct compiles.

## Result — all four BLOCKED

| API | Status | Exact compile error / evidence |
| --- | --- | --- |
| `timespec_getres` | **BLOCKED** | `error: call to undeclared function 'timespec_getres'` (c11/c17); `error: use of undeclared identifier 'timespec_getres'; did you mean 'timespec_get'?` (c2x). No libSystem symbol; no pinned source. |
| `mbrtoc8` | **BLOCKED** | `error: unknown type name 'char8_t'` + `error: call to undeclared function 'mbrtoc8'` (c11/c17), `error: use of undeclared identifier 'mbrtoc8'` (c2x). No host `<uchar.h>`; no symbol; no pinned source. |
| `c8rtomb` | **BLOCKED** | `error: call to undeclared function 'c8rtomb'` + `error: use of undeclared identifier 'char8_t'` (c11/c17), `error: use of undeclared identifier 'c8rtomb'` (c2x). No host `<uchar.h>`; no symbol; no pinned source. |
| `FE_TIESAWAY` | **BLOCKED** | `error: use of undeclared identifier 'FE_TIESAWAY'` (c11/c17/c2x). `<fenv.h>` provides only four rounding-mode macros; no ties-away encoding exists to publish. |

CAPABILITY MACROS (authoritative, in `cplus_c23_remainders.h`):

```c
#define CPLUS_C23_REMAINDERS_HAVE_TIMESPEC_GETRES 0
#define CPLUS_C23_REMAINDERS_HAVE_MBRTOC8         0
#define CPLUS_C23_REMAINDERS_HAVE_C8RTOMB         0
#define CPLUS_C23_REMAINDERS_HAVE_FE_TIESAWAY     0
#define CPLUS_C23_REMAINDERS_ALL_BLOCKED          1
```

There are deliberately **no** `cplus_*` declarations in the header and **no**
external symbols in `remainders.c`. A declaration without a backend would
merely defer failure to link time; a body would have to invent an algorithm,
and a value for `FE_TIESAWAY` would have to be fabricated. This is the same
policy already recorded in `docs/libc/C23_SMALL_UPSTREAM.md` and
`docs/libc/UCHARC23_UPSTREAM.md`.

## Host and toolchain

- `/usr/bin/clang` — Apple clang version 16.0.0 (clang-1600.0.26.6),
  target `arm64-apple-darwin27.2.0`, InstalledDir
  `/Library/Developer/CommandLineTools/usr/bin`.
- SDK: `/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk`, a symlink to
  `MacOSX15.2.sdk`. Also checked MacOSX13.3, 14.4, 14.5 and 15 SDKs.
- C standards requested: `-std=c11`, `-std=c17`, and (for completeness)
  the available draft mode `-std=c2x`. Apple clang 16 has no `-std=c23`.

## Exact probe commands and outputs

All probes compiled from stdin (`-x c -`) so no files outside the four owned
paths were created in the repository.

### timespec_getres

```sh
/usr/bin/clang -std=c11 -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk -x c - -o /dev/null <<'EOF'
#include <time.h>
int main(void){ struct timespec ts; return timespec_getres(&ts, TIME_UTC); }
EOF
```
```
<stdin>:2:44: error: call to undeclared function 'timespec_getres'; ISO C99 and later do not support implicit function declarations [-Wimplicit-function-declaration]
<stdin>:2:44: note: did you mean 'timespec_get'?
.../usr/include/_time.h:199:5: note: 'timespec_get' declared here
1 error generated.
```
Identical with `-std=c17`. With `-std=c2x`:
```
<stdin>:2:44: error: use of undeclared identifier 'timespec_getres'; did you mean 'timespec_get'?
```
Exit status 1 in all three cases.

### FE_TIESAWAY

```sh
/usr/bin/clang -std=c17 -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk -x c - -o /dev/null <<'EOF'
#include <fenv.h>
int main(void){ return FE_TIESAWAY; }
EOF
```
```
<stdin>:2:24: error: use of undeclared identifier 'FE_TIESAWAY'
1 error generated.
```
Identical with `-std=c11` and `-std=c2x`. The host `<fenv.h>` defines exactly
these rounding-mode macros: `FE_TONEAREST` = `0x00000000`, `FE_UPWARD` =
`0x00400000`, `FE_DOWNWARD` = `0x00800000`, `FE_TOWARDZERO` = `0x00C00000`
(arm64 branch; other arch branches likewise have no ties-away value).
A second compile probe confirmed all four round-trip through
`fesetround`/`fegetround`, so there is no hidden fifth mode to alias.

### mbrtoc8 / c8rtomb

```sh
/usr/bin/clang -std=c17 -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk -x c - -o /dev/null <<'EOF'
#include <wchar.h>
int main(void){ char8_t c8; mbstate_t ps = {0}; return (int)mbrtoc8(&c8, "x", 1, &ps); }
EOF
```
```
<stdin>:2:17: error: unknown type name 'char8_t'; did you mean 'wchar_t'?
<stdin>:2:61: error: call to undeclared function 'mbrtoc8'; ISO C99 and later do not support implicit function declarations [-Wimplicit-function-declaration]
<stdin>:2:61: note: did you mean 'mbrtowc'?
.../usr/include/_wchar.h:108:8: note: 'mbrtowc' declared here
2 errors generated.
```
```sh
/usr/bin/clang -std=c17 -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk -x c - -o /dev/null <<'EOF'
#include <wchar.h>
int main(void){ char buf[8]; mbstate_t ps = {0}; return (int)c8rtomb(buf, (char8_t)'x', &ps); }
EOF
```
```
<stdin>:2:62: error: call to undeclared function 'c8rtomb'; ISO C99 and later do not support implicit function declarations [-Wimplicit-function-declaration]
<stdin>:2:62: note: did you mean 'wcrtomb'?
<stdin>:2:76: error: use of undeclared identifier 'char8_t'
2 errors generated.
```
The `-std=c11` output is the same; `-std=c2x` still reports `char8_t` unknown
and `mbrtoc8`/`c8rtomb` undeclared.

## Supplementary evidence

1. Header search across every installed SDK (`grep -rlE ... --include='*.h'`,
   excluding `c++/v1`): no matches for any of the four names. The host has no
   `usr/include/uchar.h` at all.
2. The only textual hits are in libc++ headers: `<cuchar>` lines 64-65 contain
   `using ::mbrtoc8` / `using ::c8rtomb`, but they are inside
   `#if !defined(_LIBCPP_HAS_NO_C8RTOMB_MBRTOC8)`. `__config` line 1369
   unconditionally defines `_LIBCPP_HAS_NO_C8RTOMB_MBRTOC8` and only undefines
   it for glibc >= 2.36 with `__cpp_char8_t`. The `<uchar.h>` synopsis lines
   26-27 are inside a documentation comment, not declarations. C++20 probe:
   ```
   error: no member named 'mbrtoc8' in namespace 'std'
   ```
   (`char8_t` itself does work in C++20 — that probe exits 0 — so this is a
   library gap, not a compiler gap.)
3. `nm -gU` on `libSystem.B.dylib` and `libc++.dylib`, plus a grep of every
   SDK `.tbd`, found none of the four symbols.
4. Earlier audits (linked above) found no pinned upstream body: musl
   `7813e9119eb5fe681b6750b8a033a0490bca0941` / `v1.2.6` / master have no
   `src/time/timespec_getres.c` and no UTF-8 conversion sources; LLVM libc
   `llvmorg-20.1.1` has no `timespec_getres` source or build target, and its
   `fenv-macros.h` has no `FE_TIESAWAY`. The 2023 musl-ml UTF-8 patch is
   unmerged and needs non-mechanical host adaptation, so it is not eligible
   as verbatim pinned source.

## Verification of the new files (exact commands and results)

Run from the repository root:

```sh
/usr/bin/clang -std=c99 -Wall -Wextra -Wpedantic -Werror \
  -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk \
  -c libc/src/c23/remainders.c \
  -Ilibc/install-include -o /tmp/remainders.o
```
Result: exit 0, no warnings. `nm -g /tmp/remainders.o` reports no external
symbols (the unit is intentionally header/marker-only).

Direct standalone compile (no cplus_* symbol is referenced). The test is now
C99-clean; the adjacent C11 `timespec_get` probe is feature-gated so it is
compiled only when the SDK declares it:
```sh
/usr/bin/clang -std=c99 -Wall -Wextra -Wpedantic -Werror \
  -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk \
  -Ilibc/install-include tests/libc/test_c23_remainders.c \
  -o /tmp/test_c23_remainders
/tmp/test_c23_remainders
```
Result: exit 0; output under strict C99:
```
SKIP timespec_getres: undeclared in every installed SDK <time.h> (error: call to undeclared function 'timespec_getres'), no libSystem symbol, no pinned upstream source; no algorithm fabricated
SKIP mbrtoc8: no host <uchar.h>, no char8_t in C modes, no mbrtoc8 symbol in any installed SDK or dylib, no pinned upstream source; no conversion algorithm fabricated
SKIP c8rtomb: no host <uchar.h>, no char8_t in C modes, no c8rtomb symbol in any installed SDK or dylib, no pinned upstream source; no conversion algorithm fabricated
SKIP FE_TIESAWAY: <fenv.h> defines only FE_TONEAREST/FE_UPWARD/FE_DOWNWARD/FE_TOWARDZERO (error: use of undeclared identifier 'FE_TIESAWAY'); no numeric value fabricated
host timespec_get probe: unavailable under -std=c99 (C11 entry point not declared; not a C23 API result)
host fenv modes: 0,4194304,8388608,12582912 (FE_TIESAWAY absent)
host C11 multibyte: mbrtowc=1 wcrtomb=1 (C23 UTF-8 pair absent)
c23_remainders: ok (all four APIs BLOCKED, nothing fabricated)
```
The same file compiled with `-std=c11 -Wall -Wextra -Wpedantic -Werror` also
exits 0 and additionally runs the gated in host probe, printing
`host timespec_get: TIME_UTC=1 valid=1 base(0)=0 base(INT_MAX)=0`. The four
SKIP lines are identical in both modes, so gating the adjacent C11 probe does
not change the reported status of any C23 API.

Boundary cases covered: valid and invalid `timespec_get` bases (`TIME_UTC`,
`0`, `INT_MAX`) when C11+ is available; pairwise-distinct and round-tripping
host rounding modes; ASCII wide/narrow round trip, empty-string null-character
input and `errno` checks for the adjacent C11 multibyte pair. No boundary case
for the four C23 APIs themselves can exist without a host symbol or a backend.

## Standard runner integration (2026-11-23 update)

The host-only `timespec_get`/`TIME_UTC` probe used a C11 entry point that Apple
clang 16 hides under the strict `-std=c99` mode used by
`tests/libc/run_libc_tests.sh`, which made this test fail to compile there.
The TEST was fixed (no implementation, header or build-script change) by
feature-gating that probe on `__STDC_VERSION__ >= 201112L` or full Darwin C
level; under C99 the probe is omitted and a clearly-labelled host note is
printed instead, while the four authoritative SKIP lines remain unchanged.
No `cplus_*` body or fabricated rounding value was added.

Exact standard-runner verification, run from the repository root:

```sh
CPLUS_CLANG=/usr/bin/clang sh tests/libc/build_libc.sh
# -> build/libc/libcplus_hardened.a, exit 0

CPLUS_CLANG=/usr/bin/clang sh tests/libc/run_libc_tests.sh
```

Result: `ok test_c23_remainders.c`; suite summary
`libc tests: 88 passed, 2 failed` (runner exit 1). The only two failures belong
to other integration agents and are outside this file's ownership:
`test_math_c23_farith.c` and `test_math_c23_hypot3_f16.c`, both of which fail at
link time because their `cplus_*` math objects are not yet in the static
archive built by `tests/libc/build_libc.sh`. All other 88 tests, including this
one, pass.

## Exact unblock conditions

- **timespec_getres**: a host SDK whose `<time.h>` declares it AND whose
  runtime exports it (e.g. glibc >= 2.38), or a verbatim pinned upstream body
  in `libc/upstream/`; then add a distinct `cplus_timespec_getres` backend,
  switch `CPLUS_C23_REMAINDERS_HAVE_TIMESPEC_GETRES` to 1 and add real tests.
  Coordinate with `libc/src/c23_small_gaps.c`, which already defines
  `cplus_timespec_getres` when glibc >= 2.38 is detected, to avoid duplicate
  symbols.
- **mbrtoc8 / c8rtomb**: host symbols (glibc >= 2.36) or a pinned upstream
  revision that actually ships both bodies (currently none exists); then add
  backends, coordinate `char8_t` with `cplus_uchar.h`, switch both macros to 1
  and add ASCII/invalid-byte/state-reset tests.
- **FE_TIESAWAY**: the host `<fenv.h>` or the pinned compiler header must
  define the macro. Only then may a local alias be published and the macro
  switched to 1. No numeric value may be derived from the four existing
  rounding encodings.

Until one of those conditions is met, the correct observable state is four
SKIP lines and zero fabricated C23 symbols.
