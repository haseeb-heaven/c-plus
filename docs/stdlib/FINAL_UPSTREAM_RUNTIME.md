# Final runtime stdlib suites after upstream integrations

Date: 2026-11-23 (UTC `2026-11-23T04:37:50Z` when the final evidence was
collected). Branch: `agent/modern-stdlib`. This document records the ACTUAL
output of the four required runtime suites plus the explicit upstream_reuse
build verification. Every exit code and final line below was observed, not
inferred. Nothing is reported green unless the runner actually said so.

## 0. Invocation environment

All four suites were run **serially** from the repository root with a
restricted PATH and `CPLUS_BIN` set, following the established baseline
(`tests/stdlib/agent2_audit/baseline.md`):

```sh
env PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin \
    CPLUS_CLANG=/usr/bin/clang \
    CPLUS_BIN="$PWD/build/c+" \
    <suite command>
```

`build/c+` exists and is executable (`-rwxr-xr-x`, 79,699,520 bytes, built
2026-11-23 08:07). `/usr/bin/timeout` is on the restricted PATH. Raw run logs
are kept at
`/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/final_upstream_runs/`
(`deep.log`, `agent2.log`, `adversarial.log`, `abi.log`, `abi_bash5.log`,
`upstream_reuse/`).

Result-at-a-glance (exact runner exit codes):

| # | Command | Exit | Runner's own final summary line |
|---|---------|------|---------------------------------|
| 1 | `sh tests/stdlib/run_deep_tests.sh` | **1** | `deep stdlib tests: 28 passed, 58 failed, 1 skipped (29 file(s) x 3 modes)` |
| 2 | `sh tests/stdlib/agent2/run_all.sh --clean` | **1** | `SUMMARY PASS=55 FAIL=0 BUILD_FAILURE=0 TIMEOUT=1 SKIPPED=0 BLOCKED=0 TOTAL=56` |
| 3 | `sh tests/stdlib/run_adversarial.sh --suite regressions --mode strict-none` | **1** | `SUMMARY BUILD_FAILURE=2 FAIL=1 PASS=6 TIMEOUT=1 TOTAL=10` |
| 4 | `bash tests/abi/run_legacy_labs.sh` (restricted PATH) | **0 (misleading)** | **no summary line** — script aborted at startup, 0 labs ran (see §4) |

## 1. `sh tests/stdlib/run_deep_tests.sh` — exit 1

Per-mode table printed by the runner (verbatim, including the final line):

```
MODE           PASSED   FAILED  SKIPPED  BUILD-FAIL  RUN-FAIL  OUT-FAIL
default             0       29        0          29         0         0
strict-none        28        0        1           0         0         0
ubsan               0       29        0          29         0         0
deep stdlib tests: 28 passed, 58 failed, 1 skipped (29 file(s) x 3 modes)
```

Note: this is **29** `deep_*.cp` files × 3 modes = 87 invocations, not the
28×3=84 recorded in older docs — a new file (`deep_collections_alloc.cp`) has
landed. No `NO-EXPECTATION` line was emitted.

### 1.1 default and ubsan modes: external `CPLUS_EXIT_ARITHMETIC` blocker

All 29 `default` and all 29 `ubsan` invocations are **BUILD-FAIL (compiler exit
1)** — 58 build failures. The diagnostic is the external compiler/runtime
version skew already documented in
`docs/stdlib/AGENT1_EXTERNAL_RUNTIME_BLOCKER.md`,
`docs/stdlib/abi-preserving-runtime-safety.md` and
`benchmarks/safety_compare/METHODOLOGY.md`:

```
/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/cplus_runtime.c:130:23: error: use of undeclared identifier 'CPLUS_EXIT_ARITHMETIC'
  130 |     cplus_print_fatal(CPLUS_EXIT_ARITHMETIC, "signed integer overflow", NULL, 0,
/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/cplus_runtime.c:137:21: error: use of undeclared identifier 'CPLUS_EXIT_ARITHMETIC'
  137 |   cplus_print_fatal(CPLUS_EXIT_ARITHMETIC, kind, NULL, 0, msg);
error: failed to compile instrumented C+ runtime
BUILD_RC=1
```

`CPLUS_EXIT_ARITHMETIC` occurs 62 times in the deep log. This is an
**external build-time toolchain blocker, classified BUILD-FAIL by the harness**
— not a runtime failure of any stdlib test and not a green result. No
CPLUS_EXIT_ARITHMETIC occurrence appears in the strict-none runner output.

### 1.2 strict-none: 28 real passes, 1 abort misclassified as a skip

The strict-none mode built and ran 28 tests successfully. The remaining case:

```
sh: line 1: 88690 Abort trap: 6  timeout -k ... "$_bin" ...
TIMEOUT  deep_collections_alloc (strict-none): run exceeded 30s (rc=134)
```

`rc=134` is **SIGABRT** (128+6), not a wall-clock timeout. The harness classifies
any `rc >= 128` as TIMEOUT/SKIPPED, so the 1 in the `SKIPPED` column is really a
runtime abort. The binary's own stderr is:

```
C+ error: index 3 is outside the bounds of 'pre' (length 3)
```

So this is a **bounds-trap abort in the new `deep_collections_alloc` test**,
observed as SKIPPED by this harness. It is not counted as a pass.

## 2. `sh tests/stdlib/agent2/run_all.sh --clean` — exit 1

Final line (verbatim):

```
SUMMARY PASS=55 FAIL=0 BUILD_FAILURE=0 TIMEOUT=1 SKIPPED=0 BLOCKED=0 TOTAL=56
```

Exit 1 because the runner returns non-zero unless every test is PASS; the one
non-PASS is again the new collections test:

```
sh: line 1:  1116 Abort trap: 6  timeout 30s env PATH=... .../results/run/deep_collections_alloc ...
deep_collections_alloc  deep  collections  TIMEOUT  0 134  stdout differs from tests/stdlib/deep_collections_alloc.expected; stde
```

From `tests/stdlib/agent2/results/summary.json` (exact fields):
`build_exit: 0`, `runtime_exit: 134`, `classification: "TIMEOUT"`,
`stderr: "C+ error: index 3 is outside the bounds of 'pre' (length 3)\n"`,
`reason: "stdout differs from tests/stdlib/deep_collections_alloc.expected;
stderr differs from tests/stdlib/deep_collections_alloc.stderr.expected"`.
The stdout stops after `isf_lk2 1` instead of reaching the expected end. This is
the same SIGABRT as §1.2 (harness TIMEOUT label, actual signal rc=134), and it
is the only non-PASS among 56 — the other 55 are genuine PASSes (deep,
agent2 and existing groups). No CPLUS_EXIT_ARITHMETIC appears in this run
(it uses strict-none only).

## 3. `sh tests/stdlib/run_adversarial.sh --suite regressions --mode strict-none` — exit 1

Final line (verbatim):

```
SUMMARY BUILD_FAILURE=2 FAIL=1 PASS=6 TIMEOUT=1 TOTAL=10
```

Full classification block:

```
adv_alloc_failure        strict-none  PASS
adv_containers           strict-none  TIMEOUT     run rc 134
adv_exact_fit            strict-none  BUILD_FAILU compiler exit 1
adv_integer_bounds       strict-none  PASS
adv_json_fs_net          strict-none  FAIL        exit 1 != expected 0
adv_lifecycle            strict-none  PASS
adv_long_input           strict-none  BUILD_FAILU compiler exit 1
adv_null_zero            strict-none  PASS
adv_stale_views          strict-none  PASS
adv_thread_contention    strict-none  PASS
```

Exact causes:

- `adv_containers` — **TIMEOUT label, actual SIGABRT rc=134**, stderr
  `C+ error: index 40 is outside the bounds of 'present' (length 40)`. Same
  harness `rc >= 128` classification caveat as §1.2/§2; it is a bounds trap,
  not a wall-clock timeout.
- `adv_exact_fit` — **BUILD_FAILURE**, compiler exit 1 from three strict-mode
  FFI diagnostics in `tests/stdlib/regressions/adv_exact_fit.cp` (lines 59,
  69×2): `[CPLUS-FFI] pointer parameter 'p'/'a'/'b' has unverifiable extent;
  subscript cannot be proven safe`. This is the pre-existing strict error
  documented in `tests/stdlib/upstream_reuse/softbound_NOTES.md` /
  `docs/libc/STRICT_HEADER_ADAPTATION_AUDIT.md`, **not**
  CPLUS_EXIT_ARITHMETIC (0 occurrences in this run).
- `adv_long_input` — **BUILD_FAILURE**, compiler exit 1 from
  `tests/stdlib/regressions/adv_long_input.cp:46`: `[CPLUS-FFI] pointer
  parameter 'ali_buf' has unverifiable extent; subscript cannot be proven
  safe`. Also not CPLUS_EXIT_ARITHMETIC.
- `adv_json_fs_net` — **FAIL**, `exit 1 != expected 0`.
- 6 PASSes as listed. Reports: `tests/stdlib/reports/adversarial.json` and
  `tests/stdlib/reports/adversarial.txt`.

## 4. `bash tests/abi/run_legacy_labs.sh` — exit 0 but **no labs ran** (toolchain issue)

With the restricted PATH, `bash` resolves to **`/usr/bin/bash` = GNU bash
3.2.57(1)-release**, which does not support associative arrays. The script
requires bash 4+ (`declare -A` at lines 170/172) and aborts during discovery at
line 352, before it reaches the `rm -rf "$OUT"` / execution sections. Complete
output (verbatim):

```
tests/abi/run_legacy_labs.sh: line 170: declare: -A: invalid option
declare: usage: declare [-afFirtx] [-p] [name[=value] ...]
tests/abi/run_legacy_labs.sh: line 172: declare: -A: invalid option
declare: usage: declare [-afFirtx] [-p] [name[=value] ...]
tests/abi/run_legacy_labs.sh: line 352: legacy_c: unbound variable
```

There is **no terminal table, no `run_legacy_labs:` summary line and no new
evidence** from this command. Its exit code is **0**, which is misleading: an
empty execution must not be read as a green ABI result. A second invocation
reproduced exit 0 with the identical five-line output. `/opt/homebrew/bin/bash`
is 5.3.15 and would satisfy the script.

### 4.1 Supplemental diagnostic ONLY (NOT the requested `bash` command)

To establish what the labs actually do, the same script was re-run with the
homebrew bash 5.3 interpreter explicitly (`/opt/homebrew/bin/bash`), keeping
the same restricted PATH and CPLUS_BIN. This is a **different interpreter** and
does not change the §4 result; it is labelled separately on purpose:

```
env PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin \
    CPLUS_CLANG=/usr/bin/clang CPLUS_BIN="$PWD/build/c+" \
    /opt/homebrew/bin/bash tests/abi/run_legacy_labs.sh
```

Exit 0, final lines:

```
run_legacy_labs: total=54 pass=54 fail=0 build_failure=0 timeout=0 skipped=0 blocked=0
required_failures=0  summary=.../build/abi-labs/summary.json  logs=.../build/abi-labs/logs
```

`build/abi-labs/summary.json` confirms
`"counts": {"total": 54, "pass": 54, "fail": 0, "build_failure": 0,
"timeout": 0, "skipped": 0, "blocked": 0}` and `"required_failures": 0`.
All 54 discovered labs passed (legacy_c, c_to_cp, cp_to_c, precompiled,
abi_compare). This is evidence about the labs under bash 5, **not** a
replacement for the requested command's result.

## 5. Explicit upstream_reuse builds — all 8 build and pass (strict-none)

These subdirectory tests are not picked up by the top-level glob runners, so
each was built with its documented explicit command under the same restricted
PATH, with a per-invocation `timeout -k 5 30`. All eight had `BUILD_RC=0`,
`RUN_RC=0`, empty stderr, and the expected count line matching the sibling
NOTES files. Outputs were written under the temp run directory (generated
artifacts only).

| Test | Explicit build operands (`build/c+ build ...`) | Final count line |
|------|------------------------------------------------|------------------|
| `alloc_policy.cp` | `tests/stdlib/upstream_reuse/alloc_policy.cp --strict --sanitize=none -o <out>` | `ALLOC_POLICY 42` / `ALLOC_POLICY_FAILURES 0` |
| `filc_diag.cp` | `tests/stdlib/upstream_reuse/filc_diag.cp --strict --sanitize=none -o <out>` | `ALLOC_DIAG 26 0` |
| `gsl_adapt.cp` | `--strict --sanitize=none tests/stdlib/upstream_reuse/gsl_adapt.cp -o <out>` | `GSL_ADAPT 80 0` |
| `json_yyjson.cp` | `tests/stdlib/upstream_reuse/json_yyjson.cp --strict --sanitize=none -Ithird_party/yyjson -Lbuild/ext -lyyjson -o <out>` | `JSON_YYJSON 35 0` |
| `lifetime_adapt.cp` | `--strict --sanitize=none tests/stdlib/upstream_reuse/lifetime_adapt.cp -o <out>` | `LIFETIME_ADAPT 25 0` |
| `softbound.cp` | `tests/stdlib/upstream_reuse/softbound.cp --strict --sanitize=none -o <out>` | `SOFTBOUND 51 0` |
| `string_stl.cp` | `--strict --sanitize=none tests/stdlib/upstream_reuse/string_stl.cp -o <out>` | `upstream-string-checks 28` / `upstream-string-failures 0` |
| `vector_stl.cp` | `tests/stdlib/upstream_reuse/vector_stl.cp --strict --sanitize=none -o <out>` | `VECTOR_STL 14 0` |

Total: **8/8 builds succeeded**, **8/8 runs exited 0**, **0 stderr bytes**,
and the reported check counts are 42 + 26 + 80 + 35 + 25 + 51 + 28 + 14 =
**301 checks, 0 failures** across the eight self-checking tests.

### 5.1 default / UBSan classification for upstream_reuse probes

Two representative explicit probes with the same restricted PATH:

```
build/c+ build tests/stdlib/upstream_reuse/softbound.cp --strict --sanitize=undefined -o <out>   → RC=1
build/c+ build tests/stdlib/upstream_reuse/softbound.cp -o <out>                                  → RC=1
```

Both fail with the same `CPLUS_EXIT_ARITHMETIC` diagnostic at
`c-plus-core/runtime/cplus_runtime.c:130,137` and
`error: failed to compile instrumented C+ runtime`. Classification:
**external build-time toolchain/runtime-header skew → BUILD-FAIL (compiler
exit 1)**, not a stdlib runtime failure and not a pass. This matches the 58
default/UBSan build failures in §1.1. The env-only `CPLUS_INCLUDE=.../c-plus-core/runtime/include`
or build-flag `-- -DCPLUS_EXIT_ARITHMETIC=110` workarounds described in the
NOTES files were not used here, so no UBSan pass is claimed.

## 6. Scope / integrity notes

- No source file was edited by this task. The only authored file is this
  document. No `git add` / `git commit` was performed.
- The required runners do write their own generated artifacts by design;
  this run refreshed `build/stdlib-deep/`, `build/abi-labs/`,
  `tests/stdlib/agent2/results/` (including the tracked `summary.json`,
  `summary.txt`, `deep_collections_alloc.{stdout,stderr,diff}`) and
  `tests/stdlib/reports/`. Other modifications visible in `git status` are
  pre-existing work from the parallel agents, not from this task.
- Do not read any of the following as a green result: the 58
  CPLUS_EXIT_ARITHMETIC BUILD-FAILs (§1.1/§5.1); the three SIGABRT rc=134
  cases labelled TIMEOUT/SKIPPED (§1.2/§2/§3); the two `[CPLUS-FFI]`
  BUILD_FAILUREs and the `adv_json_fs_net` FAIL (§3); or the ABI runner's
  exit 0 with zero labs executed (§4). The only unambiguous greens are the 55
  agent2 PASSes, the 28 deep strict-none PASSes, the 6 adversarial PASSes, the
  54 ABI labs under bash 5 (supplemental), and the 8/8 upstream_reuse builds.
