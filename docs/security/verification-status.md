# External compiler/runtime blockers and verification status

Evidence-only record for branch `agent/modern-stdlib`. Collected 2026-11-23 by
running the requested suites **serialized** from the repository root. No
compiler, runtime, test, configuration, or source file was edited while
collecting this evidence — the only file written by this task is this document.
Every count and diagnostic below was reproduced by an actual invocation; no
result is inferred from a successful build and no green result is fabricated.

## 1. Evidence identity (provenance)

| Item | Value |
|------|-------|
| Branch | `agent/modern-stdlib` (`git rev-parse --abbrev-ref HEAD`) |
| Branch HEAD | `cbcea57174e5c6b26d07bcc4e1b5c8f7501b340a` (`git rev-parse HEAD`) |
| HEAD commit | `cbcea57174e5c6b26d07bcc4e1b5c8f7501b340a` 2026-11-23T09:16:38+05:30 — Merge remote-tracking branch 'origin/agent/modern-stdlib' into agent/modern-stdlib |
| Compiler driver | `$ROOT/build/c+`, `C+ 0.1.0`, backend host Clang/LLVM |
| `build/c+` size | `79699520` bytes |
| **`build/c+` mtime** | `2026-11-23 08:07:07` — `stat -f '%Sm \| %N' -t '%Y-%m-%d %H:%M:%S' build/c+` → `2026-11-23 08:07:07 \| build/c+` |
| External runtime source mtime | `2026-11-23 09:04:05` — `/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/cplus_runtime.c` |
| External header mtime | `2026-11-23 09:03:50` — `/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/include/cplus/cplus.h` |
| Local header mtime | `2026-11-21 06:19:18` — `runtime/include/cplus/cplus.h` |
| Host | macOS arm64 / Darwin, `uname -m = arm64`, `uname -s = Darwin` (`arm64-apple-darwin27.2.0`) |
| Backend clang (ABI suite) | `/usr/bin/clang` — Apple clang 16.0.0 (clang-1600.0.26.6), InstalledDir `/Library/Developer/CommandLineTools/usr/bin` |
| Default `clang` on PATH | `/opt/homebrew/opt/llvm/bin/clang` — Homebrew clang 23.1.6 (InstalledDir `/opt/homebrew/Cellar/llvm/23.1.6/bin`) |
| Collection time | `2026-11-23T04:00:05Z` … `2026-11-23T04:02:43Z` (UTC) |

Note on toolchain selection: `tests/abi/run_abi_compat.sh` pins
`PATH=/usr/bin:/bin` and `CPLUS_CLANG=/usr/bin/clang`, so its prerequisite
library builds use Apple clang 16 and succeed. The standalone libc and CRT
unit runners use `CC=${CPLUS_CLANG:-clang}` and therefore pick up Homebrew
clang 23.1.6 from the default PATH, which targets `arm64-apple-macosx27.0.0`
but cannot find a `MacOSX27.sdk` — this distinction explains why the ABI suite
is clean while the two C-library suites abort at their prerequisite build step.

## 2. Serialized suite results (exact final lines and exit codes)

Commands were run one at a time, in the order given, with no `|| true` masking.

| # | Command (exact) | Exit | Exact final summary line |
|---|-----------------|------|--------------------------|
| 1 | `sh tests/run_tests.sh` | **1** | `C+ tests: 99 passed, 51 failed` |
| 2 | `sh tests/libc/run_libc_tests.sh` | **1** | *(no summary line emitted — aborted at prerequisite `build_libc.sh`); final output line `1 error generated.`* |
| 3 | `sh tests/crt/unit/run_unit_tests.sh` | **1** | *(no summary line emitted — aborted at prerequisite `build_safecrt.sh`); final output line `1 error generated.`* |
| 4 | `sh tests/stdlib/run_deep_tests.sh` | **1** | `deep stdlib tests: 28 passed, 58 failed, 1 skipped (29 file(s) x 3 modes)` |
| 5 | `sh tests/abi/run_abi_compat.sh` | **0** | `abi_compat: 69 passed, 0 failed` |
| 6 | `sh tests/native/build_native_tools.sh` | **1** | `native tools: 1 built, 7 failed, 0 missing` |

### Per-suite detail

1. **`tests/run_tests.sh`** — exit 1. 99 `ok`/pass lines, 51 `FAIL` lines, all
   51 of the form `compilation failed; see tests/out/<name>.build.log`. A scan
   of `tests/out/*.build.log` shows every failing build log carries the
   `CPLUS_EXIT_ARITHMETIC` diagnostic (§3.1). Final lines:
   ```
     - bounds: compilation failed; see tests/out/bounds.build.log
     - case13_dispatch_runtime: compilation failed; see tests/out/case13_dispatch_runtime.build.log
     - classic_bug21_alignment: compilation failed; see tests/out/classic_bug21_alignment.build.log
     - classic_bug25_divzero: compilation failed; see tests/out/classic_bug25_divzero.build.log
     - classic_bug9_overflow: compilation failed; see tests/out/classic_bug9_overflow.build.log
   C+ tests: 99 passed, 51 failed
   ```

2. **`tests/libc/run_libc_tests.sh`** — exit 1, 6 output lines, no `libc tests:
   …` line. `set -eu` aborted the script inside `sh tests/libc/build_libc.sh`
   while compiling `libc/log/log.c`. Complete output:
   ```
   clang: warning: no such sysroot directory: '/Library/Developer/CommandLineTools/SDKs/MacOSX27.sdk' [-Wmissing-sysroot]
   clang: warning: using sysroot for 'macOS 27' but targeting 'arm64-apple-macosx27.0.0' [-Wincompatible-sysroot]
   /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/libc/log/log.c:27:10: fatal error: 'errno.h' file not found
      27 | #include <errno.h>
         |          ^~~~~~~~~
   1 error generated.
   ```
   No per-test result count can be reported for this suite in this run.

3. **`tests/crt/unit/run_unit_tests.sh`** — exit 1, 7 output lines, no `crt
   unit tests: …` line. `set -eu` aborted inside `sh scripts/build_safecrt.sh`
   while compiling vendored safeclib. Complete output:
   ```
   clang: warning: no such sysroot directory: '/Library/Developer/CommandLineTools/SDKs/MacOSX27.sdk' [-Wmissing-sysroot]
   clang: warning: using sysroot for 'macOS 27' but targeting 'arm64-apple-macosx27.0.0' [-Wincompatible-sysroot]
   In file included from /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/third_party/safeclib/src/extmem/timingsafe_bcmp.c:24:
   /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/third_party/safeclib/src/safeclib_private.h:98:10: fatal error: 'stdio.h' file not found
      98 | #include <stdio.h>
         |          ^~~~~~~~~
   1 error generated.
   ```
   No per-test result count can be reported for this suite in this run.

4. **`tests/stdlib/run_deep_tests.sh`** — exit 1. Final block:
   ```
   MODE           PASSED   FAILED  SKIPPED  BUILD-FAIL  RUN-FAIL  OUT-FAIL
   default             0       29        0          29         0         0
   strict-none        28        0        1           0         0         0
   ubsan               0       29        0          29         0         0
   deep stdlib tests: 28 passed, 58 failed, 1 skipped (29 file(s) x 3 modes)
   ```
   All 58 failures are `BUILD-FAIL` (29 `default` + 29 `ubsan`), each with the
   §3.1 diagnostic; there were zero `RUN-FAIL` and zero `OUT-FAIL`. The single
   `strict-none` skip is unrelated to that blocker:
   `TIMEOUT  deep_collections_alloc (strict-none): run exceeded 30s (rc=134)`
   (the harness classifies `rc>=128`/SIGABRT as SKIP; that mode has zero build
   failures).

5. **`tests/abi/run_abi_compat.sh`** — exit 0, `abi_compat: 69 passed, 0
   failed`. Final lines:
   ```
   PASS T6_neutral_plain_run
   PASS T6_neutral_cplus_run
   PASS T6_neutral_diff
   PASS T6_neutral_line_count
   abi_compat: 69 passed, 0 failed
   ```
   `build/abi/compat/results.log` confirms `RC[build_libc]=0` and the library
   builds succeeded under the pinned Apple clang 16. This is the only clean
   suite in this run.

6. **`tests/native/build_native_tools.sh`** — exit 1, final lines:
   ```
   == fuzz100_run ==
   + ./build/c+ build tests/fuzz100/run.cp --strict --sanitize=none -Ithird_party/yyjson -Lbuild/ext -lyyjson -o build/native-tools/fuzz100_run -- build/libc/libcplus_hardened.a
     FAIL fuzz100_run (compiler rc=1; see build/native-tools/fuzz100_run.build.log)

   native tools: 1 built, 7 failed, 0 missing
   ```
   Only `coverage` built; `validate_contracts`,
   `test_compiler_contract_alignment`, `test_contract_consumption`,
   `test_contract_integrity`, `run_adversarial`, `run_all`, `fuzz100_run`
   failed. All 7 failures carry the §3.3 source-level diagnostic (not the
   §3.1 runtime diagnostic: 0 of the native build logs contain
   `CPLUS_EXIT_ARITHMETIC`).

## 3. Exact external diagnostics observed

### 3.1 External compiler/runtime interface blocker — `CPLUS_EXIT_ARITHMETIC`

From `tests/out/array_len.build.log`, byte-identical across the other 50
failing `tests/out/*.build.log` files and across all 58 deep build failures
(the deep harness prints the first line prefixed by ten spaces):

```
/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/cplus_runtime.c:129:23: error: use of undeclared identifier 'CPLUS_EXIT_ARITHMETIC'
  129 |     cplus_print_fatal(CPLUS_EXIT_ARITHMETIC, "signed integer overflow", NULL, 0,
      |                       ^
1 error generated.
error: failed to compile instrumented C+ runtime
```

The instrumented build path compiles the **external** runtime source
(`/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/cplus_runtime.c`,
mtime `2026-11-23 09:04:05`) against the **local** header
(`runtime/include/cplus/cplus.h`, mtime `2026-11-21 06:19:18`), whose
`cplus_exit_code` enum ends at `CPLUS_EXIT_IO = 109` and does not define
`CPLUS_EXIT_ARITHMETIC`. This is the blocker already documented in
`docs/stdlib/AGENT1_EXTERNAL_RUNTIME_BLOCKER.md`; it remains unresolved at the
current HEAD. Attributable failures in this run: 51 (run_tests.sh) + 58
(run_deep_tests.sh build failures) = 109 invocations.

### 3.2 Host SDK / default-clang blocker — missing `errno.h` / `stdio.h`

Default `clang` on PATH is Homebrew clang 23.1.6 targeting
`arm64-apple-macosx27.0.0`, but no `MacOSX27.sdk` is installed (installed SDKs
are `MacOSX.sdk`, `MacOSX13.3.sdk`, `MacOSX14.4.sdk`, `MacOSX14.5.sdk`,
`MacOSX15.2.sdk`, `MacOSX15.sdk`). This blocks the prerequisite library builds
of `tests/libc/run_libc_tests.sh` (`errno.h`) and
`tests/crt/unit/run_unit_tests.sh` (`stdio.h`). Verbatim lines are in §2 items
2 and 3. The ABI suite avoids this by pinning `/usr/bin/clang` (Apple clang
16) with `PATH=/usr/bin:/bin`, and therefore builds the same libraries
successfully.

### 3.3 Native-tool source mismatch — `reporting`

All 7 failed native-tool builds fail at `./stdlib/error.hp:223:30: error: use
of undeclared identifier 'reporting'` (with follow-on errors at lines 224 and
226), e.g. from `build/native-tools/run_all.build.log`:

```
In file included from tests/stdlib/agent2/run_all.cp:18:
In file included from ./tests/native/cplus_test_native.hp:14:
./stdlib/error.hp:223:30: error: use of undeclared identifier 'reporting'
  223 |     if (callback != NULL && !reporting) {
      |                              ^
```

This is a source-level mismatch observed in the working tree at collection
time, distinct from §3.1 and §3.2. It is recorded as observed; no source file
was changed by this task.

## 4. Sanitizer status (honest accounting)

Backend capability is from the recorded driver-probe evidence in
`tests/stdlib/agent2/SANITIZER_MATRIX.md` (selected backend `/usr/bin/clang`,
Apple clang 16 / arm64, pinned via `CPLUS_CLANG`):

- **UNSUPPORTED by the backend** (driver compile-and-run probe rejects every
  attempt, build exit 1, no binary, no run): **ASan / `address`, Leak /
  `leak`, Thread / `thread`, Memory / `memory`, CFI / `cfi`**. Verbatim probe
  error shape:
  ```
  error: sanitizer 'address' is not supported by the selected backend (/usr/bin/clang); detected by a compile-and-run probe
  ```
  (identical with `'leak'`, `'thread'`, `'memory'`, `'cfi'`). No pass is
  claimed for any of these five.
- **Executed where applicable:** `none` (`strict-none`), `undefined`
  (UBSan), and `integer` were accepted by the probe and actually built and
  run. The recorded matrix (9 tests x 3 accepted configs = 27 runs) shows
  23 PASS (9 `strict_none` + 9 `ubsan` + 5 `integer`) and 4 FAIL under
  `--sanitize=integer` (genuine `cplus_runtime.c:174:5` unsigned-overflow
  findings in `deep_collections_alloc`, `deep_collections_boundary`,
  `deep_string_task4`, `deep_vector_task4` — run exit 134, not host abort,
  therefore FAIL rather than UNSUPPORTED and not counted as passes).
- **Important current-run caveat:** that matrix was captured at an earlier
  HEAD (`11831a2`) and was **not re-executed by this task**. In the current
  run (HEAD `cbcea571…`) the deep harness's `ubsan` mode could not build any
  of the 29 selected tests because of the §3.1 blocker, so **no fresh UBSan or
  integer sanitizer execution occurred here**. The "executed where applicable"
  statement above is recorded evidence from the matrix document, not a claim
  produced by this collection.

## 5. Working-tree check (Agent 2 ownership)

```
$ git status --short compiler runtime
$     -> (no output; exit 0)
```

`compiler/` and `runtime/` are clean: no tracked modifications and no
untracked files. The full `git status --short` shows unrelated, pre-existing
state owned by other agents (e.g. ` M stdlib/file.hp` and untracked
`scratch/bugs51.cp` … `scratch/bugs97.cp`); this task did not create or modify
any of them. **No compiler/runtime edits were made, and no `git add`/`git
commit` was performed.**

## 6. Aggregate status

**BLOCKED / NOT GREEN.** 5 of the 6 requested suites exited non-zero; only
`tests/abi/run_abi_compat.sh` is clean (exit 0). Exact aggregate from this run:

```
tests/run_tests.sh                 C+ tests: 99 passed, 51 failed                     exit 1
tests/libc/run_libc_tests.sh       (aborted at prerequisite build; no summary)        exit 1
tests/crt/unit/run_unit_tests.sh   (aborted at prerequisite build; no summary)        exit 1
tests/stdlib/run_deep_tests.sh     deep stdlib tests: 28 passed, 58 failed, 1 skipped exit 1
tests/abi/run_abi_compat.sh        abi_compat: 69 passed, 0 failed                    exit 0
tests/native/build_native_tools.sh native tools: 1 built, 7 failed, 0 missing         exit 1
```

Re-verification pass criteria: `tests/run_tests.sh` shows no `compilation
failed` lines and exit 0; `tests/stdlib/run_deep_tests.sh` shows `BUILD-FAIL`
0 in all three modes with no `CPLUS_EXIT_ARITHMETIC` diagnostic; libc/CRT
prerequisite builds succeed (or are run with the pinned Apple clang 16
backend); native tools report 8 built / 0 failed. Until then no "all green"
claim is valid.
