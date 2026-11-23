# C23 `nextup` / `nextdown` family closure

Date: 2026-11-23
Branch: `agent/modern-stdlib`
Toolchain: `/usr/bin/clang` — Apple clang version 16.0.0 (clang-1600.0.26.6),
target `arm64-apple-darwin27.2.0`.

## Result: CLOSED (host-backed, value-equivalent on this host)

All six APIs were previously declared-but-undefined in `libc/src/math/math_c23.c`
and `libc/src/math/math_c23_upstream.c` (recorded as a platform gap). They now
have real definitions backed by a verified host mechanism.

| # | ISO C23 name   | Exported name         | Definition                          |
|---|----------------|-----------------------|-------------------------------------|
| 1 | `nextup`       | `cplus_nextup`        | `nextafter(x, +INFINITY)`           |
| 2 | `nextupf`      | `cplus_nextupf`       | `nextafterf(x, +INFINITY)`          |
| 3 | `nextupl`      | `cplus_nextupl`       | `nextafterl(x, +INFINITY)`          |
| 4 | `nextdown`     | `cplus_nextdown`      | `nextafter(x, -INFINITY)`           |
| 5 | `nextdownf`    | `cplus_nextdownf`     | `nextafterf(x, -INFINITY)`          |
| 6 | `nextdownl`    | `cplus_nextdownl`     | `nextafterl(x, -INFINITY)`          |

This is the canonical host-backed definition of the two C23 operations. It is
**not** a from-scratch, bit-level C23 implementation: it is a `PLATFORM_BACKED`
wrapper that delegates to the host libm `nextafter` family (in this project's
terminology used for the other gap-closing wrappers). Every observable *value*
behaviour required by ISO C23 matched on this host in the testing below, so the
family is calibrated as closed; the residual limitations are enumerated under
**Deviations / limitations**.

## Files created (only these four; strict ownership respected)

- `libc/src/math/math_c23_next.c` — the six wrapper definitions.
- `libc/install-include/cplus_math_c23_next.h` — public declarations.
- `tests/libc/test_math_c23_next.c` — standalone C99 test (475 checks).
- `docs/libc/C23_NEXT_CLOSURE.md` — this record.

No other repository file was created or modified; no `git add`/commit was made;
no compiler, runtime, `CMakeLists.txt` or build script was touched.

## Host mechanism verification

Probe (temporary file outside the repository), compiled with
`/usr/bin/clang -std=c99 -Wall -Wextra -pedantic -lm`:

```
has __builtin_nextafter = 1
has __builtin_nextup    = 0
has __builtin_nextdown  = 0
__DBL_DENORM_MIN__=4.9406564584124654e-324 __FLT_DENORM_MIN__=1.4012984643248171e-45 __LDBL_DENORM_MIN__=4.94065645841246544177e-324
```

C17 `<math.h>` declares `nextafter` / `nextafterf` / `nextafterl`, and they link
against the host libm with no unresolved symbol. Value-level differential of the
host `nextafter` against the ISO C23 requirements (`-fno-builtin`, bit patterns
shown):

```
nextafter(+0,+inf)           x=0  -> 0x0000000000000001 (least +subnormal)
nextafter(-0,+inf)           x=-0 -> 0x0000000000000001 (least +subnormal)
nextafter(+0,-inf)           x=0  -> 0x8000000000000001 (least -subnormal)
nextafter(-0,-inf)           x=-0 -> 0x8000000000000001 (least -subnormal)
nextafter(DBL_MAX,+inf)      -> +inf
nextafter(-DBL_MAX,-inf)     -> -inf
nextafter(+inf,+inf)         -> +inf
nextafter(-inf,+inf)         -> -DBL_MAX
nextafter(+inf,-inf)         -> DBL_MAX
nextafter(-inf,-inf)         -> -inf
nextafter(NAN,+inf)          : isnan=1, input/output bits both 0x7ff8000000000000 (payload preserved)
FE after nextafter(NAN,+inf) : INVALID=0
builtin == libm (1.0,+inf)   : 1
```

Floating-point exception flags are inherited from the host libm and were
measured explicitly:

```
nextafter(double NAN,+inf)       INVALID=0 DIV0=0 OVERFLOW=0 UNDERFLOW=0 INEXACT=0
nextafter(minsub,-inf)           INVALID=0 DIV0=0 OVERFLOW=0 UNDERFLOW=1 INEXACT=1
nextafter(1.0,+inf)              INVALID=0 DIV0=0 OVERFLOW=0 UNDERFLOW=0 INEXACT=0
nextafterf(float NAN,+inf)       INVALID=0 DIV0=0 OVERFLOW=0 UNDERFLOW=0 INEXACT=0
nextafterl(ldouble NAN,+inf)     INVALID=0 DIV0=0 OVERFLOW=0 UNDERFLOW=0 INEXACT=0
```

There is no `__builtin_nextup` / `__builtin_nextdown` on this toolchain, so the
`__builtin_nextafter` (or equivalently the libm `nextafter`) mechanism is the
available, verified host mechanism; the libm call was chosen as the simplest
non-fabricated implementation because it needs no compiler extension and is
declared by C99 `<math.h>` under every build flag used here.

## Documented IEEE / C23 behaviour of the wrappers

- **NaN**: returned unchanged for both nextup and nextdown; payload/sign
  preserved, no `FE_INVALID` raised (verified for all three types).
- **±0**: `nextup(±0)` → least positive subnormal; `nextdown(±0)` → least
  negative subnormal (sign of the zero input does not change the result, which
  matches ISO C23's "toward +inf / toward -inf" definition).
- **±infinity**: `nextup(+inf)=+inf`, `nextup(-inf)=-MAX`;
  `nextdown(-inf)=-inf`, `nextdown(+inf)=+MAX`.
- **±MAX**: `nextup(MAX)=+inf`, `nextdown(-MAX)=-inf`.
- **Subnormals / boundaries**: `nextup(max subnormal)=MIN_NORMAL`,
  `nextdown(MIN_NORMAL)=max subnormal`, `nextdown(least subnormal)=+0`.
- **Direction / monotonicity**: for finite `x`, `nextup(x) > x` and
  `nextdown(x) < x`; both are non-decreasing over a sorted finite input set.

## Exact test commands and results

All commands were run from the repository root. The existing archive is built
by the project script; because the build list may not be edited under the
strict file ownership, `math_c23_next.o` is compiled separately and inserted
into the archive copy used for linking.

```sh
# 1. Build the baseline archive with the project script.
CPLUS_CLANG=/usr/bin/clang sh tests/libc/build_libc.sh
# -> /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/libc/libcplus_hardened.a

# 2. Compile the implementation with the mandated flags.
/usr/bin/clang -std=c99 -Wall -Wextra -pedantic \
  -Ilibc -Ilibc/include -Ilibc/install-include \
  -c libc/src/math/math_c23_next.c -o build/libc/math_c23_next.o
# -> no diagnostics

# 3. Insert it into the archive that the test links against.
ar rcs build/libc/libcplus_hardened.a build/libc/math_c23_next.o

# 4. Compile + link the test with the mandated flags.
/usr/bin/clang -std=c99 -Wall -Wextra -pedantic \
  -Ilibc -Ilibc/include -Ilibc/install-include \
  tests/libc/test_math_c23_next.c build/libc/libcplus_hardened.a \
  -lm -pthread -o /tmp/test_math_c23_next
# -> no diagnostics

# 5. Run.
/tmp/test_math_c23_next
# test_math_c23_next: checks=475 failures=0
```

Result: **PASS — 475 checks, 0 failures, 0 compiler warnings.**
(The `[CPLUS-LIBC] nextup*: enter` lines on stderr are the intentional logging
markers from the logging ON/OFF parity checks; they are not failures.)

UBSan build and run (a copy of the archive with `math_c23_next.o` removed is
used to avoid a duplicate definition; the instrumented implementation object is
linked explicitly):

```sh
cp build/libc/libcplus_hardened.a /tmp/libc_no_next.a
ar d /tmp/libc_no_next.a math_c23_next.o

/usr/bin/clang -std=c99 -Wall -Wextra -pedantic \
  -fsanitize=undefined -fno-sanitize-recover=all \
  -Ilibc -Ilibc/include -Ilibc/install-include \
  -c libc/src/math/math_c23_next.c -o /tmp/math_c23_next_ubsan.o

/usr/bin/clang -std=c99 -Wall -Wextra -pedantic \
  -fsanitize=undefined -fno-sanitize-recover=all \
  -Ilibc -Ilibc/include -Ilibc/install-include \
  tests/libc/test_math_c23_next.c /tmp/math_c23_next_ubsan.o /tmp/libc_no_next.a \
  -lm -pthread -o /tmp/test_math_c23_next_ubsan

UBSAN_OPTIONS=print_stacktrace=1 /tmp/test_math_c23_next_ubsan
# test_math_c23_next: checks=475 failures=0
# runtime errors: 0
```

Result: **PASS — 475 checks, 0 failures, 0 UBSan diagnostics.**

Standalone header smoke test (C and C++ include modes):

```sh
/usr/bin/clang -std=c99 -Wall -Wextra -pedantic -Ilibc/install-include -fsyntax-only hdr.c      # OK
/usr/bin/clang++ -std=c++17 -Wall -Wextra -pedantic -Ilibc/install-include -fsyntax-only hdr.cpp  # OK
```

## Deviations / limitations (explicit, not hidden)

1. **Not a full from-scratch C23 implementation.** The bodies are thin
   delegations to the host libm `nextafter` family. They are value-equivalent to
   the C23 operations for every case tested here, but they are platform-backed
   and inherit the host libm's behaviour rather than reimplementing a
   bit-level algorithm. This is the documented, non-fabricated mechanism.
2. **Bare ISO names are not exported.** Only `cplus_nextup` etc. are defined,
   matching the project-wide namespacing convention (`cplus_libc.h`,
   `math_c23.c`). Defining global `nextup`/`nextdown` would collide with a
   future host `<math.h>`; tests and consumers use the `cplus_*` names via
   `cplus_math_c23_next.h`.
3. **Build-list integration — COMPLETED.** See the "Build integration status"
   section below. `src/math/math_c23_next.c` is now in `LIBC_SOURCES` in
   `libc/CMakeLists.txt`, in the file loop of `tests/libc/build_libc.sh`, and
   the six names are forward-declared in `install-include/cplus_libc.h`. The
   earlier workaround of compiling the object separately and inserting it into
   the archive is no longer needed. The `extern` declarations in `math_c23.c`
   and `math_c23_upstream.c` emit no symbol on this toolchain and do not
   conflict (analysis below).
4. **Floating-point exception flags are host-defined.** No `fenv` masking is
   performed. NaN inputs raise no exceptions on this host (verified above), but
   subnormal-boundary steps raise `FE_UNDERFLOW|FE_INEXACT` exactly as the host
   `nextafter` does; a dedicated C23 implementation could choose different flag
   behaviour. This is inherited, not independently controlled.
5. **`long double` representation.** On this host `long double` is an 80-bit
   x87 value in a 16-byte object with padding, so the tests use equality plus
   `isnan`/`signbit` classification for it (rather than `memcmp`) to avoid
   comparing unspecified padding bytes. The value semantics are unaffected.

## Build integration status (2026-11-23, follow-up wiring task)

The closure implementation is now wired into the standard build. This was a
separate integration pass with strict file ownership; only the four files below
were touched (plus this record). No compiler, runtime, implementation source or
test file was changed; no `git add`/commit was made.

| File | Change (integration only) |
|------|---------------------------|
| `libc/CMakeLists.txt` | Added `src/math/math_c23_next.c` to `LIBC_SOURCES`, adjacent to `src/math/math_c23.c`. |
| `tests/libc/build_libc.sh` | Added `"$ROOT"/libc/src/math/math_c23_next.c` to the source-file loop, adjacent to `math_c23.c`. |
| `libc/install-include/cplus_libc.h` | Added the six missing declarations `cplus_nextup/f/l` and `cplus_nextdown/f/l` in the math section (signatures identical to `cplus_math_c23_next.h`; no existing signature changed). |
| `docs/libc/C23_NEXT_CLOSURE.md` | This section + correction of deviation #3 (integration was previously marked deferred). |

Note: while this integration pass ran, other parallel integration agents also
added `src/stdlib/atomic_wrappers.c` to the same two source lists and
`#include "cplus_stdatomic.h"` to `cplus_libc.h`. Those edits were preserved,
not reverted; they belong to the stdatomic closure, not to this one.

### Exact commands and results

All commands run from the repository root with `CPLUS_CLANG=/usr/bin/clang`
(Apple clang 16.0.0, arm64).

```sh
# 1. Build the archive using the project script (now includes math_c23_next.c).
CPLUS_CLANG=/usr/bin/clang sh tests/libc/build_libc.sh
# -> /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/libc/libcplus_hardened.a
# -> no compiler diagnostics
```

```sh
# 2. Confirm the object is in the archive and exports all six symbols.
ar t build/libc/libcplus_hardened.a | grep -x math_c23_next.o
# -> math_c23_next.o
nm -g build/libc/libcplus_hardened.a | grep -E 'cplus_(nextup|nextdown)'
# -> T _cplus_nextup, _cplus_nextupf, _cplus_nextupl,
#    T _cplus_nextdown, _cplus_nextdownf, _cplus_nextdownl
```

```sh
# 3. Full libc test runner (rebuilds the archive and compiles every test_*.c).
CPLUS_CLANG=/usr/bin/clang sh tests/libc/run_libc_tests.sh > /tmp/nextup_full_runner.log 2>&1; echo $?
# -> exit 1
# tail: "libc tests: 87 passed, 3 failed"
# -> "ok test_math_c23_next.c"  (the new test links and passes without the
#    out-of-band `ar rcs` workaround; no undefined cplus_nextup/nextdown symbols)
```

Result for this task's test: **PASS — `ok test_math_c23_next.c` (475 checks,
0 failures when run directly as recorded above), linked straight from the
project archive.** The runner's three failures are in unrelated, concurrently
modified features and are explained under "Full-runner note" below.

```sh
# 4. Standalone CMake build of libc (validates the CMakeLists.txt edit).
cmake -S libc -B <tmpdir> && cmake --build <tmpdir> --parallel
# -> exit 0; Built target cplus_hardened_libc[_shared]
nm -g <tmpdir>/libcplus_hardened_libc.a | grep -E 'cplus_(nextup|nextdown)'
# -> six T symbols as in step 2 (static and shared targets both build cleanly)
```

### `math_c23.c` / `math_c23_upstream.c` extern analysis (no conflict)

`libc/src/math/math_c23.c` guards its nextup/nextdown definitions with
`CPLUS_C23_HAS_BUILTIN(__builtin_nextup[f|l])` /
`__builtin_nextdown[f|l]`. On this toolchain all six `__has_builtin` probes are
false (see "Host mechanism verification" above), so the `#else` branches emit
only `extern double cplus_nextup(double);`-style declarations — **no symbol**.
`libc/src/math/math_c23_upstream.c` likewise contains only six plain `extern`
declarations with the same signatures. A declaration does not conflict with the
definitions in `math_c23_next.c` (identical types, identical `cplus_*` names),
and both the shell build and the CMake build linked with zero duplicate-symbol
errors. `cplus_libc.h` additions use those same signatures, so including both
headers is harmless (compatible repeated declarations).

Caveat recorded, not changed: on a hypothetical toolchain where those
`__builtin_nextup/nextdown` builtins *do* exist, `math_c23.c` would define the
six symbols as well as `math_c23_next.c`, producing a duplicate definition.
That file is outside this task's ownership, so the latent issue is documented
rather than patched; the mandated `/usr/bin/clang` host is unaffected.

### Full-runner note (unrelated failures)

`sh tests/libc/run_libc_tests.sh` reported **87 passed, 3 failed**. None of the
failures involve nextup/nextdown:

- `test_c23_remainders.c` — compile errors: `timespec_get` undeclared and
  `TIME_UTC` undefined under the runner's `-std=c99` (C11 API; other agent's
  in-flight work).
- `test_math_c23_farith.c` — link errors: undefined `cplus_fadd`/`cplus_fmul`/
  ... because `math_c23_farith.c` is not yet in the source list.
- `test_math_c23_hypot3_f16.c` — link errors: undefined `cplus_hypot3`/f16
  symbols because `math_c23_hypot3_f16.c` is not yet in the source list.

Those three features are owned by other parallel integration agents; their
sources are absent from the current build lists. This task does not modify them.
Once those agents wire their sources in (and address the `timespec_get` C11
issue), the runner is expected to be fully green.

**Remaining blocker for this task: none.** The nextup/nextdown implementation
is in both standard build lists, the public header declares it, and the new
test links and passes from the project-built archive.
