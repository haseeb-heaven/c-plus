# System API safety suite — runner, evidence and limitations

Branch: `agent/modern-stdlib`. Snapshot: 2026-11-23 (repository root
`/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib`). This document records
**observed** runner output only. No runtime, compiler, header or test source
was edited by this task; no `git add`/`git commit` was performed.

Files authored by this task:

- `tests/stdlib/system_safety/run.sh`
- `tests/stdlib/system_safety/README.md`
- `docs/stdlib/system-api-safety.md` (this file)

All other files in `tests/stdlib/system_safety/` were authored concurrently by
other agents; their counts and pass/fail state below are point-in-time
observations, not a claim about the final branch state.

---

## 1. Runner contract

`run.sh` is a POSIX `sh` script (`set -u`, deliberately **no** `set -e`). It:

1. discovers every `tests/stdlib/system_safety/*.cp` file at run time (no hard-coded list);
2. builds each with the repo-built `build/c+`, bounded by `timeout -k 5 30`;
3. runs each binary under the same bound, feeding `<name>.stdin` when present;
4. captures build stdout/stderr, program stdout, program stderr and both exit codes;
5. classifies each case as `PASS`, `FAIL`, `BUILD_FAILURE`, `TIMEOUT`, `SKIPPED`
   or `BLOCKED`, then continues with the next case/mode;
6. writes a terminal table and a machine-readable JSON summary at
   `build/system-safety/summary.json` (all artifacts under the gitignored
   `build/` tree).

There is no failure-suppressing short-circuit in the script: every command's
return code is captured explicitly. A timeout (`rc 124` or signal termination)
is reported as `TIMEOUT`, never as a pass. A build that fails with the external
`CPLUS_EXIT_ARITHMETIC` mismatch (Section 3) is `BLOCKED`, not silently
attributed to the test.

Exit policy: `0` only when at least one case passed and there are zero
`FAIL`/`BUILD_FAILURE`/`TIMEOUT`/`BLOCKED` cases; `1` for any of those statuses;
`2` for setup/usage errors (missing `timeout`/`diff`/compiler, no tests found,
`--only` matching nothing, unknown mode/option). `SKIPPED` is reported but not
counted as a pass.

Modes and exact build commands (`<extra>` = `-I<root>/third_party/yyjson
-L<root>/build/ext -lyyjson` for JSON/yyjson harnesses, as three argv operands):

```sh
timeout -k 5 30 build/c+ build <src> --strict --sanitize=none <extra> -o <bin>  # strict-none (default)
timeout -k 5 30 build/c+ build <src> <extra> -o <bin>                          # default
timeout -k 5 30 build/c+ build <src> --strict --sanitize=undefined <extra> -o <bin>  # ubsan
timeout -k 5 30 <bin> < <name>.stdin   # when <name>.stdin exists; else </dev/null
```

CLI: `--only NAME` (exact stem or substring, repeatable), `--mode
strict-none,default,ubsan|all`, `--json PATH`, `--json-stdout`, `--help`.
Fixtures: `<name>.stdin`, `<name>.expected`, `<name>.stderr.expected`,
`<name>.exit`/`<name>.rc`, `<name>.skip` (reason = first line). For a harness
without output fixtures, a pass requires exit code `0`, empty stderr and — when
the last stdout line is `<UPPER_TAG> <checks> <failures>` — `<failures> == 0`.
`SKIP_REASON ...` stdout lines are recorded in JSON as informational
sub-check skips, never as a whole-test skip.

---

## 2. Exact commands and observed output

All commands were run from the repository root unless noted.

### V1 — syntax and forbidden-construct checks

```sh
sh -n tests/stdlib/system_safety/run.sh && echo "SYNTAX OK"
grep -n '|| true' tests/stdlib/system_safety/run.sh; echo "grep rc=$?"
```

Observed: `SYNTAX OK`; `grep rc=1` (no match). The script uses `set -u` only.

### V2 — full suite, strict-none (snapshot with 18 discovered files)

```sh
sh tests/stdlib/system_safety/run.sh; echo "EXIT:$?"
```

Exact output:

```text
system_safety: 18 test file(s), modes: strict-none
MODE           NAME                     STATUS         DETAIL
strict-none    error_hub_core           PASS           ERROR_HUB_CORE 45 0
strict-none    error_hub_system         PASS           ERROR_HUB_SYSTEM 27 0
strict-none    fs_basic_safety          PASS           FS_SAFETY 36 0
strict-none    fs_longpath_safety       PASS           FS_SAFETY 29 0
strict-none    fs_symlink_safety        PASS           FS_SAFETY 29 0
strict-none    io_file_empty_large      PASS           IO_FILE_EMPTY_LARGE 35 0
strict-none    io_file_lines            BUILD_FAILURE  /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/tests/stdlib/system_safety/io_file_lines.cp:175:24: error: [CPLUS-LIFETIME] variable 'ifln_st' is uninitialized (read before it is initialized) 
strict-none    json_lifecycle           PASS           JSON_LIFECYCLE 18 0
strict-none    json_numeric_edges       PASS           JSON_NUMERIC_EDGES 28 0
strict-none    json_parse_edges         PASS           JSON_PARSE_EDGES 28 0
strict-none    net_invalid              PASS           NET_SAFETY 6 0
strict-none    net_partial              PASS           NET_SAFETY 4 0
strict-none    net_refusal              PASS           NET_SAFETY 4 0
strict-none    net_signal               PASS           NET_SAFETY 3 0
strict-none    thread_condvar           PASS           THREAD_SAFETY 16 0
strict-none    thread_contention        PASS           THREAD_SAFETY 10 0
strict-none    thread_deadlock          PASS           THREAD_SAFETY 4 0
strict-none    thread_lifecycle         PASS           THREAD_SAFETY 28 0

MODE               PASS     FAIL    BUILD  TIMEOUT     SKIP  BLOCKED
strict-none          17        0        1        0        0        0
system_safety: 17 passed, 0 failed, 1 build-failures, 0 timeouts, 0 skipped, 0 blocked (18 file(s) x 1 mode(s))
JSON summary: build/system-safety/summary.json
system_safety suite: FAIL (required cases did not pass)
EXIT:1
```

The nonzero exit is correct: `io_file_lines.cp` was rejected by C+ strict mode
(`[CPLUS-LIFETIME] variable 'ifln_st' is uninitialized`). This is a test-local
build failure, classified `BUILD_FAILURE`, not masked. An earlier snapshot
while the corpus was still being written (11 files) produced `10 passed, 1
failed, 0 build-failures` with `net_partial` printing `NET_SAFETY 4 1`; that
harness was subsequently changed by its owner and passed in the 18-file
snapshot. A later snapshot (24 files, after more harnesses were added and
several were edited concurrently) ended with:

```text
system_safety: 20 passed, 2 failed, 2 build-failures, 0 timeouts, 0 skipped, 0 blocked (24 file(s) x 1 mode(s))
JSON summary: build/system-safety/summary.json
system_safety suite: FAIL (required cases did not pass)
EXIT:1
```

with `io_file_fd_exhaustion` and `io_file_lines` as `FAIL`, and
`error_hub_multi_cb` / `thread_lifecycle` as `BUILD_FAILURE`. All observations
are recorded; none of those harnesses was edited here.

### V3 — mode selection and the external blocker in the runner

```sh
sh tests/stdlib/system_safety/run.sh --only net_invalid --mode strict-none,default,ubsan; echo "EXIT:$?"
```

Exact output:

```text
system_safety: 1 test file(s), modes: strict-none default ubsan
MODE           NAME                     STATUS         DETAIL
strict-none    net_invalid              PASS           NET_SAFETY 6 0
default        net_invalid              BLOCKED        external blocker: c-plus-core runtime uses CPLUS_EXIT_ARITHMETIC; absent from this branch runtime header
ubsan          net_invalid              BLOCKED        external blocker: c-plus-core runtime uses CPLUS_EXIT_ARITHMETIC; absent from this branch runtime header

MODE               PASS     FAIL    BUILD  TIMEOUT     SKIP  BLOCKED
strict-none           1        0        0        0        0        0
default               0        0        0        0        0        1
ubsan                 0        0        0        0        0        1
system_safety: 1 passed, 0 failed, 0 build-failures, 0 timeouts, 0 skipped, 2 blocked (1 file(s) x 3 mode(s))
JSON summary: build/system-safety/summary.json
system_safety suite: FAIL (required cases did not pass)
EXIT:1
```

### V4 — machine-readable summary parses

```sh
python3 -c 'import json; d=json.load(open("build/system-safety/summary.json")); print("valid json:", d["suite"], d["counts"], len(d["cases"]))'
```

Observed after V2:
`valid json: system_safety {'PASS': 1, 'FAIL': 0, 'BUILD_FAILURE': 0, 'TIMEOUT': 0, 'SKIPPED': 0, 'BLOCKED': 2} 3`
(the file is overwritten per invocation, so this read reflects V3's
1-file/3-mode run; the schema and case array are valid in both).

### V5 — direct compiler probes behind the BLOCKED classification

```sh
timeout -k 5 30 ./build/c+ build tests/stdlib/eof.cp --strict --sanitize=none -o build/system-safety-probe
echo "rc=$?"                                  # rc=0, no output
timeout -k 5 30 ./build/c+ build tests/stdlib/eof.cp -o build/system-safety-probe-default
timeout -k 5 30 ./build/c+ build tests/stdlib/eof.cp --strict --sanitize=undefined -o build/system-safety-probe-ubsan
```

Default and UBSan builds both exit `1` with:

```text
/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/cplus_runtime.c:129:23: error: use of undeclared identifier 'CPLUS_EXIT_ARITHMETIC'
  129 |     cplus_print_fatal(CPLUS_EXIT_ARITHMETIC, "signed integer overflow", NULL, 0,
      |                       ^
1 error generated.
error: failed to compile instrumented C+ runtime
```

`--strict --sanitize=none` does not compile the instrumented runtime, which is
why strict-none builds are unaffected.

### V6 — isolated runner self-test for every status

A synthetic copy of `run.sh` was placed in a temporary directory outside the
repository with a fake `build/c+` and six fake harnesses. No repository file
was changed. Command: `sh <tmp>/tests/stdlib/system_safety/run.sh` (with
`SS_RUN_TIMEOUT=30`). Exact output:

```text
system_safety: 6 test file(s), modes: strict-none
MODE           NAME                     STATUS         DETAIL
strict-none    badbuild                 BUILD_FAILURE  fake compile error
strict-none    block                    BLOCKED        external blocker: c-plus-core runtime uses CPLUS_EXIT_ARITHMETIC; absent from this branch runtime header
strict-none    fail                     FAIL           exit 1 (expected 0) self-reported-failures=1
strict-none    pass                     PASS           OK_SUMMARY 1 0
strict-none    skip                     SKIPPED        synthetic skip for runner validation
strict-none    slow                     TIMEOUT        run exceeded 30s (rc=124)

MODE               PASS     FAIL    BUILD  TIMEOUT     SKIP  BLOCKED
strict-none           1        1        1        1        1        1
system_safety: 1 passed, 1 failed, 1 build-failures, 1 timeouts, 1 skipped, 1 blocked (6 file(s) x 1 mode(s))
JSON summary: build/system-safety/summary.json
system_safety suite: FAIL (required cases did not pass)
```

All six cases ran (no early abort), all six statuses were emitted, the JSON
case array carried the same statuses, and the exit was `1`. A `--only pass`
re-run exited `0`; `--only does_not_exist` and `--mode nonsense` exited `2`.

---

## 3. Current external blocker (do not edit the runtime)

`../c-plus-core/runtime/cplus_runtime.c:129` calls
`cplus_print_fatal(CPLUS_EXIT_ARITHMETIC, "signed integer overflow", ...)`.
That enum value exists in the core repository's
`c-plus-core/runtime/include/cplus/cplus.h:85` (`CPLUS_EXIT_ARITHMETIC = 110`)
but **not** in this branch's `runtime/include/cplus/cplus.h`, whose
`cplus_exit_code` enum ends at `CPLUS_EXIT_IO = 109` (line 79). The driver
compiles the shared, instrumented core runtime against this branch's header,
so every `default` and `ubsan` build fails with the diagnostic in V5. This is
also recorded independently in `docs/stdlib/container_safety/VECTOR_AUDIT.md`
(section 2.1 caveat), where strict-none kept passing while default/UBSan
builds failed.

This is an out-of-scope, in-progress compiler/runtime working-tree state. It
is not a defect in any `system_safety` harness and this task must not fix it:
no runtime header or core runtime file was edited. Consequences for evidence:

- only `strict-none` currently produces executable system-safety evidence;
- `default` and `ubsan` cases are reported `BLOCKED`, never `PASS`;
- no claim is made that sanitizer modes pass until the external header/runtime
  mismatch is resolved by its owners.

---

## 4. Honest limitations

- **Concurrent corpus.** Harnesses were being authored by parallel agents
  during validation. Counts changed from 11 files (10 PASS / 1 FAIL) to 18
  files (17 PASS / 1 BUILD_FAILURE) between two runs. The tables above are
  snapshots; a later branch state may differ.
- **Evidence strength varies by harness.** Most harnesses are self-checking
  (final `<TAG> <checks> <failures>` line) with no checked-in `.expected`
  fixture. Those are verified by exit code + empty stderr + self-report, not
  by byte-exact output comparison. The JSON `expectation` field records which
  evidence was used for each case. Harnesses with `.expected` files (if any are
  added later) are additionally diffed byte-for-byte.
- **One build failure remains in the observed snapshot.** `io_file_lines.cp`
  was rejected at build time with a `[CPLUS-LIFETIME]` diagnostic. The runner
  reports it faithfully; fixing the harness is its owner's responsibility.
- **Failures are not root-caused here.** A `FAIL`/`BUILD_FAILURE` means the
  runner observed a violation of the declared contract; it is not, by itself,
  proof of a stdlib bug versus a harness bug versus an environment issue.
- **No external services.** Network harnesses use local sockets/forked peers;
  this suite does not establish remote connectivity or exercise a live game.
- **Single summary file.** `build/system-safety/summary.json` is overwritten
  on each invocation; copy it if a prior run's evidence must be retained.
  Logs and binaries are likewise keyed by `<name>_<mode>` and overwritten.
- **`SKIPPED` is not green.** An explicit `.skip` marker is reported and
  counted separately; a run with no `PASS` at all exits nonzero.
- **Toolchain assumptions.** `/usr/bin/timeout` (BSD `timeout` in this
  environment), `diff`, and the repo-built `./build/c+` must be available;
  otherwise the runner exits `2` rather than running unbounded tests.
- **No source/runtime edits.** This task changed only the three files listed
  at the top. In particular, `runtime/include/cplus/cplus.h` was left untouched
  despite the blocker in Section 3.

## 5. Result summary for this task

- Files created: 3 (runner, test-dir README, this document).
- Runner validation: syntax OK; all six statuses exercised; JSON parsed;
  `--only`, `--mode`, exit codes 0/1/2 exercised.
- Blocker: external `CPLUS_EXIT_ARITHMETIC` mismatch — `default` and `ubsan`
  modes BLOCKED; `strict-none` builds succeed.
- Final observed full-suite snapshot: 18 discovered files, 17 `PASS`,
  0 `FAIL`, 1 `BUILD_FAILURE`, 0 `TIMEOUT`, 0 `SKIPPED`, 0 `BLOCKED`,
  overall exit `1`.
