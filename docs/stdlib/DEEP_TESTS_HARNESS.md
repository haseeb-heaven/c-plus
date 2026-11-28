# Deep stdlib tests: contracts and harness requirements

This is the harness-level contract for the `tests/stdlib/deep_*.cp` suites on the
`agent/modern-stdlib` branch. It records the inventory, the exact commands, the
fatal/recoverable rules, and the fault-injection discipline. Per-family
behavioural findings and expected stdout live in
`docs/stdlib/DEEP_<FAMILY>_CONTRACT.md`, not here.

> **In-progress status (document snapshot).** Six deep `.cp` files are present
> and are being written by sibling agents; none are committed yet. No
> `docs/stdlib/DEEP_*_CONTRACT.md` file existed when this document was written.
> The per-family contract documents and the driver flag work described in
> §3.2/§3.3 are marked **in progress** below; do not treat them as finished
> results. Re-check `build/c+ --help` and `ls docs/stdlib/` before relying on
> them.

---

## 1. Deep test inventory

All paths are relative to the repository root. "stdin fixture" means the
sibling `tests/stdlib/<name>.stdin` file consumed by the harness when present;
if none exists the harness redirects `/dev/null`.

| Test file | Header family | Scenario class | Expected outcome (process level) | stdin fixture | Per-family contract |
| --- | --- | --- | --- | --- | --- |
| `tests/stdlib/deep_error_vector.cp` | `stdlib/error.hp`, `stdlib/vector.hp` | success (E1, E2, E4, E5, V1–V3); recoverable-failure (E3, V4–V6); fatal-safety: none executed — the trailing `F` tag is only a summary marker | **success** — exit 0, no fatal fired | none (`/dev/null`) | `docs/stdlib/DEEP_ERROR_VECTOR_CONTRACT.md` (**in progress**) |
| `tests/stdlib/deep_collections_growth.cp` | `stdlib/collections.hp`, `stdlib/error.hp` | success; recoverable-failure (deterministic growth allocation failure + atomicity/leak check via telemetry); fatal-safety: none executed | **success** — reaching the `RESULT` line under an external timeout proves loop termination | none (`/dev/null`) | `docs/stdlib/DEEP_COLLECTIONS_CONTRACT.md` (**in progress**) |
| `tests/stdlib/deep_collections_collisions.cp` | `stdlib/collections.hp`, `stdlib/error.hp` | success (collision-heavy insert/remove/reinsert for `intmap`, `intset`, `strmap`); termination proof; fatal-safety: none executed | **success** — exit 0 under an external timeout | none (`/dev/null`) | `docs/stdlib/DEEP_COLLECTIONS_CONTRACT.md` (**in progress**) |
| `tests/stdlib/deep_file_lifecycle.cp` | `stdlib/file.hp`, `stdlib/error.hp` | success; recoverable-failure (invalid args, double close, failed ops, injected allocation failure, fd/temp cleanup); fatal-safety cases are **not executed** — compile-time copy rejection and runtime ownership fatals are recorded as one-shot invocations in the contract doc | **success** — exit 0; fd count must return to baseline | none (`/dev/null`) | `docs/stdlib/DEEP_FILE_CONTRACT.md` (**in progress**) |
| `tests/stdlib/deep_file_lines.cp` | `stdlib/file.hp`, `stdlib/error.hp` | success (one-byte buffer, CRLF, exact fit); recoverable-failure (`STDLIB_FAIL` + `STDLIB_CATEGORY_BOUNDS` for overlong/oversized, single EOF without error) | **success** — exit 0, no hang in `read_line` | `tests/stdlib/deep_file_lines.stdin` (`alpha` / 36-char line / `beta`) | `docs/stdlib/DEEP_FILE_CONTRACT.md` (**in progress**) |
| `tests/stdlib/deep_string_append.cp` | `stdlib/string.hp`, `stdlib/error.hp` | success; recoverable-failure (guard paths with `STDLIB_ALLOC_LIMIT` reduced to 4096); **fatal-safety** (forked child, exact `CPLUS_EXIT_DOUBLE_FREE` / `CPLUS_EXIT_TYPE_CONFUSION`, parent stays alive) | **fatal-safety** sub-scenario contained in a child; process-level outcome is still exit 0 | none (`/dev/null`) | `docs/stdlib/DEEP_STRING_CONTRACT.md` (**in progress**) |

Classification vocabulary used in this document:

- **success** — documented happy path; the call returns `STDLIB_OK` / `0` and
  state advances.
- **recoverable-failure** — documented refusal: `STDLIB_FAIL` (`-1`), a `NULL`
  allocation result, or `STDLIB_EOF` (`1`), with a specific error category and
  exit code; the process must survive.
- **fatal-safety** — a deliberate violation whose only correct outcome is a
  runtime fatal exit or a compile-time rejection. Never executed in the main
  test process.

Conventions that every deep test must keep:

- Self-checking: each check prints a deterministic record; the process returns
  `0` only when every check passed and `1` otherwise. Deep tests intentionally
  ship **no `.expected` file** — the harness only checks the exit code and
  stderr cleanliness.
- Deterministic: no wall-clock-dependent behaviour, no random seeds, no live
  network, no host-dependent paths. Temp files are created under a path
  supplied via environment/argument (default under `build/stdlib-tests/`) and
  removed on every exit path.
- Generic `src/` code is never changed to make a deep test green; see §7.

---

## 2. Harness behaviour (what `run_stdlib_tests.sh` actually does)

`tests/stdlib/run_stdlib_tests.sh` is a plain POSIX `sh` driver:

- It globs `tests/stdlib/*.cp`, skipping `bench_*`, and filters names with a
  **substring** match against `$1`. Therefore `deep_` selects every deep test
  and `deep_string` selects `deep_string_append` (and any future
  `deep_string_view`).
- Compilation: `"$CPLUS" build "$src" $libs -o "$exe"`, where
  `CPLUS=${CPLUS_BIN:-"$ROOT/build/c+"}`. Link libraries are inferred from
  `#include "stdlib/...hp"` lines. All current deep tests need only the default
  runtime library; none require SDL3/curl/libuv/FFmpeg.
- Execution: stdin from `<name>.stdin` when present, else `/dev/null`; stdout
  and stderr are captured under `build/stdlib-tests/`. A non-zero exit is a
  **FAIL**, a `.expected` file is diffed when present, and a summary line
  `C+ stdlib tests: N passed, M failed, K skipped` is printed. Final exit code
  is `1` if any test failed.
- **Gap (in progress):** the harness does **not** currently wrap test binaries
  in `timeout`. Two deep-test comments assume it does
  (`deep_collections_collisions.cp`, `deep_file_lines.cp`). Until a timeout
  wrapper lands, deep tests must self-bound with a bounded loop and/or
  `alarm()` (§4), otherwise a hung probe hangs the suite.

Build artifacts belong in `build/stdlib-tests/` (gitignored); never vendor a
deep-test fixture into `tests/stdlib/data/` or a user's home directory.

---

## 3. Exact commands

All commands assume the repository root as CWD and a built compiler.

### 3.0 Prerequisites

```sh
cmake -S . -B build
cmake --build build --target c+ -- -j4
# Optional: override the compiler used by the harness
CPLUS_BIN="$PWD/build/c+" tests/stdlib/run_stdlib_tests.sh deep_
```

### 3.1 Default mode

Default C+ mode is **Safe**: the driver lowers `.cp`/`.hp`, enables `-O0 -g`,
and (from `compiler/Driver/Driver.cpp`, `backendArgs()`) appends
`-fsanitize=undefined,bounds,null,alignment -fno-sanitize-recover=all`
plus the `-Werror=*` safety warnings. UBSan is therefore **already on** in
default mode; there is no separate default-mode sanitizer command.

```sh
# Single deep test through the harness (recommended)
tests/stdlib/run_stdlib_tests.sh deep_error_vector

# Every deep_* test through the harness
tests/stdlib/run_stdlib_tests.sh deep_

# Direct single-TU build/run (bypasses the harness; useful for a bisect)
build/c+ build tests/stdlib/deep_error_vector.cp \
  -o build/stdlib-tests/deep_error_vector
build/stdlib-tests/deep_error_vector < /dev/null
```

### 3.2 STRICT mode with sanitizers disabled

**Driver-flag status (in progress).** The `build/c+` binary present at document
time advertises `--default`, `--strict`, `--sanitize=LIST`
(`none|address|undefined|memory|thread|leak|cfi|integer`),
and `--no-sanitize` in `build/c+ --help`. The checked-in
`compiler/Main.cpp` still documents only `--release|--debug|--legacy-c`, so
these flags are a sibling agent's in-progress driver change. Verify with
`build/c+ --help` after a fresh rebuild; if the flags are absent, use the
pass-through fallback below.

Preferred form when the flags exist (verified with the current `build/c+`):

```sh
build/c+ build tests/stdlib/deep_error_vector.cp \
  --strict --no-sanitize \
  -o build/stdlib-tests/deep_error_vector.strict-nosan
```

`--strict` is the fail-closed contract mode (reject unverified operations);
`--no-sanitize` is the alias for `--sanitize=none`. The related library/runtime
setting for the safe-C mapping layer is
`cplus_safe_set_unknown_capacity_mode(1)` (`stdlib/safe/safe_memory.h`), with
the libc equivalent `cplus_libc_set_unknown_capacity_mode(1)`
(`libc/src/string/bounded.c`). Deep `.hp` tests express strict expectations as
exact `error.category()` / `error.code()` assertions, not by mutating library
globals.

Fallback when the driver has no strict/sanitize flags — pass the
Clang-compatible flag through the driver's `--` boundary. This is confirmed to
override the driver's default `-fsanitize=...` because pass-through args are
appended after the backend base args:

```sh
build/c+ build tests/stdlib/deep_error_vector.cp \
  -o build/stdlib-tests/deep_error_vector.nosan \
  -- -fno-sanitize=all
```

To run the whole deep subset through the harness in this mode, point
`CPLUS_BIN` at a tiny wrapper that injects the flags (the harness itself has no
mode option):

```sh
cat > /tmp/cplus-deep-strict-nosan <<'EOF'
#!/bin/sh
exec /absolute/path/to/c-plus/build/c+ "$@" --strict --no-sanitize
EOF
chmod +x /tmp/cplus-deep-strict-nosan
CPLUS_BIN=/tmp/cplus-deep-strict-nosan tests/stdlib/run_stdlib_tests.sh deep_
```

If using the fallback driver, replace the `exec` line's trailing flags with
`-- -fno-sanitize=all`.

### 3.3 ASan + UBSan (where supported)

Host facts (verified in `docs/libc/ASAN_AUDIT.md` and locally):

- Apple clang 16 (`/usr/bin/clang`) ASan aborts during startup with
  `AddressSanitizer: CHECK failed: sanitizer_malloc_mac.inc:189`; it is not
  usable for ASan here.
- Homebrew clang 23 (`/opt/homebrew/opt/llvm/bin/clang`, found as `clang` on
  PATH) links and runs ASan **only with an explicit existing SDK**:
  `/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk`.
- The built driver's `--sanitize=address` probe rejects address on this host
  for **both** clang binaries (`error: sanitizer 'address' is not supported by
  the selected backend ...`), so do not rely on that flag for ASan. The
  pass-through form below works and was verified to compile and run cleanly.

```sh
SDK=/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk
CPLUS_CLANG=/opt/homebrew/opt/llvm/bin/clang \
build/c+ build tests/stdlib/deep_string_append.cp \
  --strict --no-sanitize \
  -o build/stdlib-tests/deep_string_append.asan \
  -- -fsanitize=address,undefined -fno-omit-frame-pointer -isysroot "$SDK"
ASAN_OPTIONS=detect_leaks=1 \
  ./build/stdlib-tests/deep_string_append.asan \
  < /dev/null
```

If the driver lacks `--no-sanitize`, drop it; the built-in UBSan flags combine
harmlessly with pass-through `-fsanitize=address,undefined`. Do **not** add
`-fsanitize-recover` — the fatal/recoverable classification in §4/§5 assumes
`-fno-sanitize-recover=all`.

UBSan-only verification is the default Safe-mode run from §3.1; when the new
driver flags are available, `--sanitize=undefined` is an equivalent explicit
spelling (verified to build). Linux CI is the authoritative ASan path — see
`.github/workflows/libc-crt.yml` ("ASan+UBSan+TSan smoke"), which uses
`clang -O1 -g -pthread -fsanitize=address,undefined ...` directly.

### 3.4 Harness subset command

```sh
tests/stdlib/run_stdlib_tests.sh deep_<prefix>
# examples:
tests/stdlib/run_stdlib_tests.sh deep_file      # deep_file_lines + deep_file_lifecycle
tests/stdlib/run_stdlib_tests.sh deep_collections
tests/stdlib/run_stdlib_tests.sh deep_          # all six current deep tests
```

Logs and captures: `build/stdlib-tests/<name>.build.log`, `<name>.stdout`,
`<name>.stderr`, `<name>.diff`. Expected result for a green deep subset: every
line `ok <name>`, summary with `0 failed`, shell exit code 0.

---

## 4. Rules for fatal-safety tests

A fatal case is any scenario whose correct behaviour is process termination

- `cplus_fatal()` in `runtime/cplus_runtime.c` ends with
  `_Exit((int)code)`, i.e. one of the exact `CPLUS_EXIT_*` values from
  `runtime/include/cplus/cplus.h`: `101` BOUNDS, `102` NULL_POINTER, `103`
  USE_AFTER_FREE, `104` DOUBLE_FREE, `105` TYPE_CONFUSION, `106` OWNERSHIP,
  `107` RESOURCE, `108` UNSUPPORTED, `109` IO.
- A UBSan abort is `SIGABRT`, which the `tests/run_tests.sh` runtime_fail path
  records as shell exit `134` (`128 + 6`). `tests/runtime_fail/<name>.exit`
  pins either form (e.g. `alloc_overflow.exit = 107`, `double_release.exit =
  104`; the default is `134`).

Rules:

1. **Subprocess only.** Fork a child (`fork()` + `waitpid()`), or invoke a
   one-shot binary from the driver/harness. The parent test process must never
   execute the deliberate violation. `deep_string_append.cp` already does this
   for `CPLUS_EXIT_DOUBLE_FREE` (104) and `CPLUS_EXIT_TYPE_CONFUSION` (105),
   checking `WIFEXITED(st) && WEXITSTATUS(st) == expected`.
2. **Bounded wait / timeout.** Arm `alarm(N)` in the child (small N, e.g. 5 s)
   and poll `waitpid(..., WNOHANG)` with a deadline in the parent; on timeout,
   `kill(pid, SIGKILL)`, reap, and record `TIMEOUT` as a failure. A bare
   blocking `waitpid()` is not sufficient. *(Gap: the current
   `deep_string_append.cp` forks and checks exact codes but uses an unbounded
   `waitpid`; adding the alarm/deadline is in progress.)*
3. **Record exact exit codes.** Assert `WIFEXITED` + `WEXITSTATUS == expected
   CPLUS_EXIT_*` (or the pinned `134` for a UBSan abort), and separately report
   `WIFSIGNALED` with `WTERMSIG`. Print the observed code so the per-family
   contract doc can quote it verbatim.
4. **Never destabilise the suite.** `fflush(stdout)` before `fork()`; the child
   must use `_Exit(code)`, not `exit()`, so it cannot flush inherited buffers;
   the parent must reap every child and continue checking. A fatal's stderr is
   captured under `build/stdlib-tests/` and is never interpreted as a suite
   failure by itself.
5. **Compile-time fatals are not runtime deep tests.** Copying an owning
   `file`, for example, belongs in a one-shot `c+ check` invocation recorded in
   `docs/stdlib/DEEP_FILE_CONTRACT.md`, not in a `deep_*.cp` runner.
6. A fatal-safety deep test is green only when the parent prints its final
   success record and exits `0`; an unreachable "expected fatal" that never
   fires is a failure.

---

## 5. Rules for recoverable failures

Every refusal must be asserted on all four axes:

1. **Exact status.** `STDLIB_FAIL` (`-1`) for failing methods; `NULL` for
   `stdlib_alloc` / `stdlib_calloc`; `STDLIB_EOF` (`1`) for end-of-input (not a
   failure). Never accept a generic "nonzero" as proof.
2. **Exact error category/status.** Check `error.category()` against
   `STDLIB_CATEGORY_INVALID|RANGE|IO|MEMORY|BOUNDS|OWNERSHIP|LIFECYCLE|UTF8`
   (`STDLIB_CATEGORY_OK` on reset), and `error.code()` / the callback's
   `cplus_exit_code` against the exact `CPLUS_EXIT_*` value. `error_reset()`
   must restore `STDLIB_CATEGORY_OK`, `CPLUS_EXIT_OK`, and an empty message.
   Cached failures (`failed_now()`) and one-shot (`failed()`) semantics must be
   checked separately where the API defines them.
3. **Resource state after failure.** Container `size()`/`capacity()` and
   element values must be unchanged; views/slices must be `stale()` and not
   `valid()` where the contract says so; no double release; telemetry must
   show `alloc_count == free_count` after all `drop()` calls, and
   `alloc_failures` incremented once per injected failure. `deep_file_lifecycle.cp`
   additionally compares the open-fd count before/after.
4. **No leaked temp files.** Any file a deep test creates is created under a
   caller-supplied path (default in `build/stdlib-tests/`), is `unlink`ed on
   success **and** on every failure branch, and is never placed in `data/` or
   a user-specific directory. A test that leaves a temp file behind is a
   failure even if all other checks pass.
5. A recoverable failure must not print sanitizer diagnostics or a C+ runtime
   fatal; if it does, the finding goes to the per-family contract doc.

---

## 6. Deterministic fault injection

Use only the helpers in `stdlib/error.hp` (checked allocator, lines 224–251):

```c
void stdlib_alloc_fail_at(long n);    /* 1-based: the n-th allocation from now fails once */
void stdlib_alloc_fail_after(long n); /* n allocations succeed, every later one fails */
void stdlib_alloc_fail_reset(void);   /* clear fail_at, fail_from and seq */
long stdlib_alloc_seq(void);          /* allocation attempts since the last reset */
```

Semantics are implemented in `stdlib_alloc()`: it bumps `stdlib_state.seq`,
then refuses with `NULL` and records `stdlib_note(STDLIB_CATEGORY_MEMORY,
CPLUS_EXIT_RESOURCE, "allocation failure (injected)")` before ever calling
`cplus_alloc()`. `stdlib_calloc()` is overflow-checked and delegates to
`stdlib_alloc()`.

Rules:

- **Never exhaust system memory.** Injection is a logical counter, not real
  OOM. Do not loop allocating until `malloc` fails, do not raise the process
  rlimit, and do not rely on host free-memory. `stdlib_alloc()` itself refuses
  requests above `STDLIB_ALLOC_LIMIT` (default `1 << 40`; deliberately reduced
  to `4096` inside `deep_string_append.cp` so overflow branches are tiny and
  fast — this is the documented allocator override, not an API change).
- Always pair an injection with `stdlib_alloc_fail_reset()` in the same scope,
  even on the failure path, so subsequent deep tests in the same process start
  clean. Use `stdlib_alloc_seq()` if you need to enumerate every allocation
  point of an operation.
- Assert the refusal is observable: `NULL` result, `STDLIB_FAIL` from the
  container method, unchanged size/capacity, `tm.alloc_failures >= 1`, and no
  leak (`alloc_count == free_count` after drops). Then reset and assert the
  operation succeeds — recovery is part of the contract (`deep_error_vector.cp`
  V4, `deep_collections_growth.cp`).
- Do not inject from a background thread; `stdlib_state` is shared and the
  callback has a per-thread re-entrancy guard. Keep injection on the
  game/test main thread and keep callback bodies allocation-free.
- For libc/safe-C suites only, the equivalent symbols are
  `cplus_alloc_fail_after/reset` (`libc/install-include/cplus_libc.h`) and
  `cplus_safe_alloc_fail_after/reset` (`stdlib/safe/safe_alloc.h`). Deep `.hp`
  tests must use the `stdlib_*` forms.

---

## 7. No API changes to pass tests

**No API behaviour was changed merely to pass tests.** Every behaviour change
associated with this deep-test work has a reproduced finding in the relevant
per-family `docs/stdlib/DEEP_*_CONTRACT.md` document: the exact reproduction
command, observed versus expected status/category/exit code, and sanitizer
output where relevant. A deep test that cannot pass against the existing API
is reported as a finding and blocks the change; it is never "fixed" by
relaxing the API, weakening a guard, or editing a generic file under `stdlib/`
without first recording that finding. Contract docs are the authoritative
place for the finding; this file records only the harness rules.

---

## 8. Report template

Copy this block into the handoff report and fill it in. Do not claim live-game
or cross-platform results from a local build alone.

```text
Deep stdlib test report — <date> — branch agent/modern-stdlib

New deep .cp tests: <N>
  - tests/stdlib/deep_<name>.cp  (<header family>, <scenario class>, <stdin fixture or none>)
  - ...

Results (default mode, tests/stdlib/run_stdlib_tests.sh deep_):
  pass: <n>   fail: <n>   skip: <n>
  failing tests: <names + exit codes / stderr first line>

STRICT + no sanitizers:
  command: <exact command from §3.2>
  pass: <n>   fail: <n>   skip: <n>

ASan+UBSan (host + toolchain):
  command: <exact command from §3.3>
  pass: <n>   fail: <n>   skip: <n>
  unsupported/skipped: <reason, e.g. Apple clang 16 ASan CHECK failure>

Fatal-safety evidence:
  <test>: <exact expected exit code> -> <observed exit code>, <timeout: yes/no>
Recoverable-failure evidence:
  <test>: <status> / <category> / <CPLUS_EXIT_*>, resource state: <...>, temp files: none

Exact reproduction commands:
  1) cmake -S . -B build && cmake --build build --target c+ -- -j4
  2) tests/stdlib/run_stdlib_tests.sh deep_<prefix>
  3) <strict/no-sanitize command>
  4) <ASan+UBSan command>

Contract docs updated/created:
  - docs/stdlib/DEEP_<FAMILY>_CONTRACT.md  (<finding IDs / in progress>)
Known limitations:
  - <e.g. harness has no timeout wrapper yet; --strict/--sanitize flags in progress>
```

---

## 9. Real paths referenced

- Harness: `tests/stdlib/run_stdlib_tests.sh` (subset selection at `matches()`,
  single-TU loop at line 149, summary at line 277).
- Non-stdlib analogue for fatal tests: `tests/run_tests.sh` (runtime_fail loop,
  `.exit` / `.stderr` contract) and `tests/runtime_fail/*.exit`.
- Fault injection + categories + telemetry: `stdlib/error.hp` (categories at
  lines 39–53, `STDLIB_ALLOC_LIMIT` at 57–67, state at 109–125,
  `stdlib_alloc_fail_at/after/reset` at 226–245, `stdlib_alloc()` at 364–392).
- Fatal exit codes: `runtime/include/cplus/cplus.h` (`cplus_exit_code`),
  `runtime/cplus_runtime.c` (`cplus_fatal()` → `_Exit(code)` at line 44).
- Compiler flags: `compiler/Main.cpp` (`--release/--debug/--legacy-c`; new
  `--strict/--sanitize` flags are in progress), `compiler/Driver/Driver.cpp`
  (`backendArgs()` adds UBSan in Safe mode at lines 499–519).
- Sanitizer evidence: `docs/libc/ASAN_AUDIT.md`, `docs/libc/COVERAGE.md`,
  `docs/THREAT_MODEL.md`, `docs/LIMITATIONS.md`, `.github/workflows/libc-crt.yml`.
- Safe/strict runtime knobs: `stdlib/safe/safe_memory.h`,
  `libc/src/string/bounded.c`.
