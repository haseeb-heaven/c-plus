# Agent 1 coordination: external compiler/runtime blocker on a fully green suite

Scope note: this document is evidence-only. It records a cross-repository
interface mismatch found while running the stdlib suites on branch
`agent/modern-stdlib`. No compiler, runtime, test source, or configuration file
was modified while collecting it; running the suites only refreshed generated
logs under `tests/out/` and `build/stdlib-deep/`. No claim below is inferred
from a successful build: every count and diagnostic was reproduced by running
the suites.

## Blocker at a glance

The sanitizer build path of the compiler compiles the **external** runtime
source at `/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/cplus_runtime.c`
against the **local** header at
`/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/runtime/include/cplus/cplus.h`.
The external source uses `CPLUS_EXIT_ARITHMETIC` (line 129); the local header
does not define it. Every instrumented runtime compilation therefore fails
before any test binary is produced, so the suites cannot be green until Agent 1
aligns that interface.

## Observed environment

| Item | Observed value |
|------|----------------|
| Branch | `agent/modern-stdlib` (`git rev-parse --abbrev-ref HEAD`) |
| Compiler binary | `build/c+`, 79,699,520 bytes |
| `build/c+` mtime | `2026-11-23 08:07:07` (`stat -f '%Sm | %N' -t '%Y-%m-%d %H:%M:%S' build/c+` → `2026-11-23 08:07:07 | build/c+`) |
| External runtime source mtime | `2026-11-23 09:04` (`/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/cplus_runtime.c`) |
| External header mtime | `2026-11-23 09:03:50` (`/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/include/cplus/cplus.h`) |

## Exact failing commands and results

```sh
sh tests/run_tests.sh
```

Result: `C+ tests: 99 passed, 51 failed`. All 51 failures are
`compilation failed; see tests/out/<name>.build.log`; a scan of
`tests/out/*.build.log` found 51 files and **all 51** contain the
`CPLUS_EXIT_ARITHMETIC` diagnostic below.

```sh
sh tests/stdlib/run_deep_tests.sh
```

Result (reproduced identically on two runs):

```
MODE           PASSED   FAILED  SKIPPED  BUILD-FAIL  RUN-FAIL  OUT-FAIL
default             0       29        0          29         0         0
strict-none        28       0        1           0         0         0
ubsan               0       29        0          29         0         0
deep stdlib tests: 28 passed, 58 failed, 1 skipped (29 file(s) x 3 modes)
```

All 58 deep failures are `BUILD-FAIL` (29 in `default`, 29 in `ubsan`), each
carrying the same diagnostic. The single `strict-none` skip is separate from
this blocker: `TIMEOUT  deep_collections_alloc (strict-none): run exceeded 30s (rc=134)`;
the harness classifies `rc>=128` (SIGABRT) as SKIP, and that mode has zero
build failures.

### Downstream counts attributable to this blocker

| Suite | Invocations affected | Current outcome |
|-------|----------------------|-----------------|
| `tests/run_tests.sh` | 51 | 51 compilation failures (99 still pass) |
| `tests/stdlib/run_deep_tests.sh` | 58 | 58 build failures (28 pass, 1 unrelated skip) |

## Exact diagnostic

From `tests/out/array_len.build.log` (byte-identical in the other 50 failing
build logs and in the deep build logs):

```
/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/cplus_runtime.c:129:23: error: use of undeclared identifier 'CPLUS_EXIT_ARITHMETIC'
  129 |     cplus_print_fatal(CPLUS_EXIT_ARITHMETIC, "signed integer overflow", NULL, 0,
      |                       ^
1 error generated.
error: failed to compile instrumented C+ runtime
```

The deep harness prints the same first line prefixed by ten spaces, followed by
`error: failed to compile instrumented C+ runtime`.

## Root cause: header actually consumed by that runtime source

A `--verbose` build shows the exact backend command. The instrumented runtime
source is the external file, while the only C+ include path is the local one:

```
-I /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/../runtime/include
...
-x c /Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/cplus_runtime.c
```

Reproduce with:

```sh
timeout 90 ./build/c+ build tests/stdlib/deep_string_view.cp \
  --strict --sanitize=undefined -o build/stdlib-deep/_probe --verbose 2>&1 \
  | grep -E 'clang|cplus_runtime|CPLUS_EXIT_ARITHMETIC| -I'
```

`build/../runtime/include` resolves to this repo's `runtime/include`, whose
`cplus/cplus.h` is the header the external source `#include`s. Driver logic in
the external compiler is consistent with this: `findSourceRoot()` prefers the
compiled-in `CPLUS_SOURCE_DIR` (`/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core`)
while `findCPlusIncludeDir()` uses `CPLUS_INCLUDE` (unset in this shell) and then
`findByLayout("runtime/include")`, i.e. the layout walked upward from the
executable, which is `c-plus-stdlib/build/c+`.

### Constant absent from the local header

`grep -rn "CPLUS_EXIT_ARITHMETIC" .` over this repo's sources found **no match**
in any `.h`, `.c`, or `.cp` file; the only matches were the generated
`tests/out/*.build.log` files quoted above. The local enum ends at
`CPLUS_EXIT_IO = 109`:

```c
/* runtime/include/cplus/cplus.h, lines 69-79 */
typedef enum {
  CPLUS_EXIT_OK = 0,
  CPLUS_EXIT_BOUNDS = 101,
  ...
  CPLUS_EXIT_UNSUPPORTED = 108,
  CPLUS_EXIT_IO = 109
} cplus_exit_code;
```

By contrast, the external header already declares it:
`/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/include/cplus/cplus.h:85`
reads `CPLUS_EXIT_ARITHMETIC = 110`. The external runtime therefore expects a
newer header than the one this repo's compiler binary passes on its `-I` path.

## Git status: no local compiler/runtime edits

```
$ git status --short -- compiler runtime
$ git diff --stat -- compiler runtime
```

Both commands produced **no output**: `compiler/` and `runtime/` are clean in
this repository, with no tracked modifications and no untracked files. The full
`git status --short` at collection time showed only 47 untracked scratch files
(`scratch/bugs51.cp` … `scratch/bugs97.cp`) and no tracked modifications; other
parallel agents may add their own untracked work afterwards. The modifications
visible in the sibling `c-plus-core` checkout (read-only
observation; `git -C /Users/haseeb-mir/Documents/Code/Cpp/c-plus-core status --short`)
are outside this repository and are not part of this change.

## Coordination boundary

**Agent 2 will not edit compiler/runtime without coordination.** The compiler
and runtime source of truth is the external `c-plus-core` checkout, owned by
Agent 1. Agent 2's work stays in the stdlib/test/docs layer; any interface
change must be agreed with and made by Agent 1.

## Minimal interface change required from Agent 1

Pick one; option A is the smallest file change and keeps ABI values aligned.

- **Option A (define the constant in the consumed header):** add
  `CPLUS_EXIT_ARITHMETIC = 110` to the `cplus_exit_code` enum in this repo's
  `runtime/include/cplus/cplus.h` (value must match the external header's line
  85), so the external `cplus_runtime.c` compiles against the header actually
  passed via `-I .../c-plus-stdlib/runtime/include`. Keep
  `cplus_exit_code`'s existing enumerator values unchanged.
- **Option B (align include paths):** make the compiler pass the external
  include directory instead — e.g. run the suites with
  `CPLUS_INCLUDE=/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/include`
  exported, or rebuild/adjust `c+` so `findByLayout("runtime/include")` resolves
  to `c-plus-core/runtime/include`. This uses the header that already defines
  the constant and requires no header edit, but the chosen resolution must be
  deterministic for both test runners.

Do **not** fix this by editing `c-plus-core/runtime/cplus_runtime.c` to avoid the
constant without Agent 1's agreement: that source already declares the intended
arithmetic-exit interface, and changing it would mask the header mismatch.

## Re-verification commands (exact)

Run from this repo root with the same `build/c+` that was used above:

```sh
sh tests/run_tests.sh
sh tests/stdlib/run_deep_tests.sh
```

Pass criteria:

- `tests/run_tests.sh`: no `compilation failed` lines; exit status 0; expect the
  current 51 `BUILD`-phase failures to pass (99 already pass).
- `tests/stdlib/run_deep_tests.sh`: `BUILD-FAIL` column 0 for all three modes,
  exit status 0 or 2 only if the pre-existing `deep_collections_alloc`
  (strict-none, rc=134) skip remains; the 58 `CPLUS_EXIT_ARITHMETIC` build
  failures must be gone.

For a one-line isolated recheck of the interface itself:

```sh
timeout 90 ./build/c+ build tests/stdlib/deep_string_view.cp \
  --strict --sanitize=undefined -o build/stdlib-deep/_probe --verbose 2>&1 \
  | grep -F CPLUS_EXIT_ARITHMETIC
```

Expected after the fix: no diagnostic emitted by that command.
