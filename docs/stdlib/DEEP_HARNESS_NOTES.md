# Deep stdlib harness notes

This document records the reliability contract of
`tests/stdlib/run_deep_tests.sh`: the exact commands it runs, how it classifies
outcomes, and why it is written that way. It is the harness-level companion to
`docs/stdlib/DEEP_TESTS_HARNESS.md`. Per-family behavioural contracts live in
`docs/stdlib/DEEP_<FAMILY>_CONTRACT.md`.

Scope of this change: **only** `tests/stdlib/run_deep_tests.sh` and this file.
No `.cp` test, `.hp` header, compiler source, or build script was modified.

---

## 1. Invocation

From the repository root:

```sh
# All deep tests, all three modes (the default)
sh tests/stdlib/run_deep_tests.sh

# NEW: pattern argument — substring match on the .cp base name
sh tests/stdlib/run_deep_tests.sh deep_net
sh tests/stdlib/run_deep_tests.sh deep_io deep_file   # multiple patterns = OR
```

With no argument every `tests/stdlib/deep_*.cp` is selected. Each argument is
a **substring** filter on the base name (without `.cp`), matching the
convention already used by `tests/stdlib/run_stdlib_tests.sh`. A test is run
if its name contains *any* supplied pattern. If nothing matches, the harness
prints an error and exits **1** — a typo must never look green.

The compiler and limits are overridable without editing the script:

```sh
CPLUS_BIN=/abs/path/to/c+        # default: $ROOT/build/c+
DEEP_OUT=/abs/path/to/outdir     # default: $ROOT/build/stdlib-deep
DEEP_BUILD_TIMEOUT=60            # seconds, default 60
DEEP_RUN_TIMEOUT=30              # seconds, default 30
DEEP_KILL_AFTER=5                # seconds, default 5 (SIGTERM -> SIGKILL)
```

These knobs change *budgets*, never the pass/fail criteria: a test still has to
build and exit 0 in every mode to count as passed.

The `timeout` utility is a hard prerequisite; the harness aborts with exit 1
before running anything if it is missing. `PATH` is pinned to
`/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:$PATH` so the same toolchain
and `timeout` are used on every run.

## 2. Modes and exact commands

Every selected test is built and run in three modes. The flags were checked
against the local driver's `build/c+ --help`, which advertises `--strict`,
`--sanitize=LIST` (`none|address|undefined|...`) and `--no-sanitize`.

| Mode key | Display label | Compiler flags |
| --- | --- | --- |
| `default` | `default` | *(none — driver DEFAULT/Safe mode)* |
| `strict_none` | `strict-none` | `--strict --sanitize=none` |
| `ubsan` | `ubsan` | `--strict --sanitize=undefined` |

Mode keys use underscores internally because POSIX `sh` identifiers cannot
contain `-`; the display label restores the hyphen.

For a test named `<name>` with source `tests/stdlib/<name>.cp`, the effective
commands per mode are:

```sh
# Build (always bounded). $flags is the mode's column above; $extra is the
# yyjson include/link triplet for deep_json* tests, empty otherwise.
timeout -k 5 60 "$CPLUS" build tests/stdlib/<name>.cp $flags $extra \
    -o build/stdlib-deep/<name>_<mode> \
    >build/stdlib-deep/<name>_<mode>.build.log 2>&1

# Run (always bounded). stdin is <name>.stdin when present, else /dev/null.
timeout -k 5 30 build/stdlib-deep/<name>_<mode> \
    <stdin >build/stdlib-deep/<name>_<mode>.out \
    2>build/stdlib-deep/<name>_<mode>.err
```

The yyjson triplet, needed by
`deep_json_stress` and `deep_json_types`, is exactly:

```sh
-I$ROOT/third_party/yyjson -L$ROOT/build/ext -lyyjson
```

— the same flags `run_stdlib_tests.sh` infers from `#include "stdlib/json.hp"`.

## 3. Outcome classification

`timeout` is used **without `--preserve-status`**, so its own 124 sentinel is
preserved when the limit expires. `-k 5` escalates to SIGKILL so a probe that
ignores SIGTERM cannot outlive its budget; such a termination surfaces as
137 (128 + SIGKILL).

The exit code is captured directly (`cmd; rc=$?`), never through `if ! cmd`,
which would collapse it to 0/1. Classification is identical for the build and
run phases:

| Exit code | Meaning | Bucket | Line prefix |
| --- | --- | --- | --- |
| `0` | completed successfully | **passed** | `ok` |
| `124` | `timeout` limit expired | **skipped** | `TIMEOUT` |
| `>= 128` | killed by signal (incl. `-k` escalation, 137) | **skipped** | `TIMEOUT` |
| any other non-zero | real failure; value printed verbatim | **failed** | `BUILD-FAIL` / `RUN-FAIL` |

A failed or timed-out build **never** proceeds to the run phase. A build
failure and a runtime failure are distinct events: they carry distinct
prefixes and distinct counters, so a compiler regression cannot be confused
with a test/UBSan abort.

The report is emitted per mode:

```
MODE           PASSED   FAILED  SKIPPED  BUILD-FAIL  RUN-FAIL
default             1        0        0           0         0
strict-none         1        0        0           0         0
ubsan               1        0        0           0         0
```

`passed + failed + skipped` is the number of selected tests in that mode.
`BUILD-FAIL + RUN-FAIL == FAILED`, and `SKIPPED` counts only timeouts.

Harness process exit status:

| Status | Condition |
| --- | --- |
| `0` | every selected invocation passed (no failures, no skips) |
| `1` | at least one build/runtime failure, or no test matched the pattern |
| `2` | no failures, but at least one invocation timed out/skipped |

Exit 2 is deliberately non-zero: **an unverified test is not a green test**.
The exact child exit codes are always printed on the detail lines, so callers
and log readers never lose the original value.

## 4. Rationale (what was fixed)

1. **Every compilation and execution is bounded.** The build and run commands
   both use `timeout`. Adding `-k` matters: plain `timeout` sends SIGTERM and
   then waits indefinitely, so a test that ignores SIGTERM would still hang the
   suite. Escalation makes the bound real, and the 137 result is classified as
   a timeout, not a pass.
2. **Build failures separated from runtime failures.** The old script printed
   `FAIL ... build` and `FAIL ... exit` into one undifferentiated `fail`
   counter. They now have separate prefixes and separate `BUILD-FAIL` /
   `RUN-FAIL` columns.
3. **Per-mode accounting.** The old script printed one global tally across all
   three modes, so a single mode's regression was invisible. Each mode now has
   its own passed/failed/skipped row (plus the phase split).
4. **A timeout is never a pass.** Only exit `0` increments `passed`. Codes 124
   and 128+ go to `skipped`, and a skips-only run exits 2 rather than 0.
5. **Exact exit codes preserved.** `if ! timeout ...; then` erased the child's
   status; the replacement captures `$?` immediately and prints it verbatim on
   `BUILD-FAIL` / `RUN-FAIL` / `TIMEOUT` lines. The harness itself uses 0/1/2
   as documented above.
6. **Pattern argument.** `sh tests/stdlib/run_deep_tests.sh deep_net` (or any
   substring, including several) works; zero matches is an error, not a
   0-test green run.
7. **No checks weakened.** Mode flags, the yyjson link flags, stdin fixtures,
   `/dev/null` fallback, output capture, and the "exit 0 required" criterion
   are unchanged from the original harness. Only accounting, bounding, and
   selection were added.

## 5. Verification

All commands below were run from the repository root on branch
`agent/modern-stdlib`.

### 5.1 Pattern run against the real compiler

```sh
sh -n tests/stdlib/run_deep_tests.sh
sh tests/stdlib/run_deep_tests.sh deep_net; echo "SCRIPT_EXIT=$?"
```

Actual output:

```
deep stdlib harness: build<=60s, run<=30s, kill-after=5s, compiler=/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/c+
pattern(s): deep_net
ok       deep_net (default): SUMMARY 8 6 0 0
ok       deep_net (strict-none): SUMMARY 8 6 0 0
ok       deep_net (ubsan): SUMMARY 8 6 0 0

MODE           PASSED   FAILED  SKIPPED  BUILD-FAIL  RUN-FAIL
default             1        0        0           0         0
strict-none         1        0        0           0         0
ubsan               1        0        0           0         0
deep stdlib tests: 3 passed, 0 failed, 0 skipped (1 file(s) x 3 modes)
SCRIPT_EXIT=0
```

### 5.2 Failure and timeout paths (stub compiler, outside the repo)

A stub compiler was written to a temporary directory and selected via
`CPLUS_BIN`; it returns chosen exit codes or stalls. No repository test or
header file was changed for these probes.

```sh
STUB=/tmp/.../stub-cplus       # actual path used:
                               # /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/deep-harness-verify/stub-cplus

CPLUS_BIN=$STUB STUB_MODE=run_fail sh tests/stdlib/run_deep_tests.sh deep_net
# -> RUN-FAIL ... exit 7 ; failed=3, RUN-FAIL=3 ; SCRIPT_EXIT=1

CPLUS_BIN=$STUB STUB_MODE=build_fail sh tests/stdlib/run_deep_tests.sh deep_net
# -> BUILD-FAIL ... compiler exit 9 ; failed=3, BUILD-FAIL=3 ; SCRIPT_EXIT=1

CPLUS_BIN=$STUB STUB_MODE=run_hang DEEP_RUN_TIMEOUT=1 DEEP_KILL_AFTER=1 \
  sh tests/stdlib/run_deep_tests.sh deep_net
# -> TIMEOUT ... run exceeded 1s (rc=124) ; skipped=3 ; SCRIPT_EXIT=2

CPLUS_BIN=$STUB STUB_MODE=build_hang DEEP_BUILD_TIMEOUT=1 DEEP_KILL_AFTER=1 \
  sh tests/stdlib/run_deep_tests.sh deep_net
# -> TIMEOUT ... build exceeded 1s (rc=124) ; skipped=3 ; SCRIPT_EXIT=2

CPLUS_BIN=$STUB STUB_MODE=run_ignore_term DEEP_RUN_TIMEOUT=1 DEEP_KILL_AFTER=1 \
  sh tests/stdlib/run_deep_tests.sh deep_net
# -> TIMEOUT ... (rc=137) ; skipped=3 ; SCRIPT_EXIT=2

sh tests/stdlib/run_deep_tests.sh no_such_test_pattern
# -> error: no deep_*.cp matched pattern(s): no_such_test_pattern ; SCRIPT_EXIT=1

CPLUS_BIN=$STUB STUB_MODE=ok sh tests/stdlib/run_deep_tests.sh deep_net deep_io_1byte
# -> 2 file(s) x 3 modes, 6 passed ; SCRIPT_EXIT=0
```

Observed summary lines for these probes:

```
RUN-FAIL deep_net (default): exit 7                 # exact code preserved
BUILD-FAIL deep_net (default): compiler exit 9      # build vs runtime separated
TIMEOUT  deep_net (default): run exceeded 1s (rc=124)
TIMEOUT  deep_net (default): build exceeded 1s (rc=124)
TIMEOUT  deep_net (default): run exceeded 1s (rc=137)   # SIGKILL escalation
deep stdlib tests: 0 passed, 0 failed, 3 skipped (1 file(s) x 3 modes)
note: 3 invocation(s) timed out and were SKIPPED, not counted as passes
deep stdlib tests: 6 passed, 0 failed, 0 skipped (2 file(s) x 3 modes)
```

The `Killed: 9` line occasionally emitted by the shell for the escalated
process is informational; the harness still classifies rc 137 as `TIMEOUT`
and never as a pass.
