# Final verification of the core test suites

Date: 2026-11-23. Branch: `agent/modern-stdlib`. This document records the
ACTUAL output of the six requested suite runners. Every exit code and final
line below was observed by running the command, not inferred or copied from an
older report. Nothing is called green unless the runner itself said so.

## 0. Invocation environment

All six commands were run **serially** from the repository root (`/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib`), each wrapped in
`timeout 900`, with `SDKROOT` unset, `CPLUS_CLANG=/usr/bin/clang`, and the
branch's canonical restricted PATH:

```sh
env -u SDKROOT PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin \
    CPLUS_CLANG=/usr/bin/clang \
    CPLUS_BIN="$PWD/build/c+" \
    timeout 900 sh <runner>
```

That PATH is the one documented repeatedly across this repo (e.g.
`docs/stdlib/FINAL_UPSTREAM_RUNTIME.md`, `tests/stdlib/agent2_audit/baseline.md`).
`build/c+` exists and is executable. `/usr/bin/timeout` is on the PATH; Apple
clang 16 (`/usr/bin/clang`) is authoritative and Homebrew clang is excluded.

Raw, unedited run logs are kept at
`/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/final_suites_verification/`
(`01_libc.log`, `02_crt_unit.log`, `03_method_tests.log`,
`04_rust_examples.log`, `04b_rust_examples_toolchain.log`,
`05_adversarial_1000.log`, `06_safety_compare.log`).

## 1. Result-at-a-glance

| # | Suite command | Exact exit | Final line (verbatim) | Interpretation |
|---|---------------|-----------:|------------------------|----------------|
| 1 | `sh tests/libc/run_libc_tests.sh` | **0** | `libc tests: 109 passed, 0 failed` | **GREEN.** All 109 libc tests passed, 0 failed. |
| 2 | `sh tests/crt/unit/run_unit_tests.sh` | **0** | `crt unit tests: 85 passed, 0 failed` | **GREEN.** All 85 CRT unit tests passed, 0 failed. |
| 3 | `sh tests/stdlib/method_tests/run_all.sh` | **0** | `aggregate: ok (518 methods passed, 0 skipped)` | **GREEN.** 8 groups, 518 methods, 0 failed, 0 skipped. |
| 4 | `sh benchmarks/rust_examples/run_all.sh` | **1** | `RESULT: FAIL` | **NOT green — but 0 real mismatches.** Under the canonical restricted PATH 6 groups are `NO_REPORT` because `rustc` is not on that PATH (`~/.cargo/bin` excluded); 20 rows are reported BLOCKED (never passes). A re-run with the toolchain dirs on a still-restricted PATH exits 0 / `RESULT: PASS` with 29 BLOCKED and 0 BAD_REPORT. See §5. |
| 5 | `sh tests/adversarial_1000/run_all.sh` | **0** | `adv1000-agg: groups=8 scenarios=8000 (target 9000) refused=5209 failures=0 hard_fail=0` (last literal line is the `adv1000-agg: logs: ...` pointer, see §6) | **GREEN, with a target-count caveat.** 8 groups x 1000 = 8000 scenarios actually executed; the script's declared target is 9000. 0 failures, 0 hard failures. |
| 6 | `sh benchmarks/safety_compare/run_all.sh` | **1** | `note:    blocked rows are external-toolchain blockers (CPLUS_EXIT_ARITHMETIC), not passes` | **NOT green by exit code.** `counts: ok=9 blocked=5 fail=6 required_failures=1`; exit 1 is driven by `required_failures=1`. The 5 BLOCKED rows are external `CPLUS_EXIT_ARITHMETIC` runtime-header skew, not passes. See §7. |

Honest summary: suites 1, 2, 3 and 5 exit 0. Suites 4 and 6 exit 1. Neither
nonzero exit is a fabricated green, and neither is evidence of a real
behavioural regression: suite 4's failures are missing-`rustc`-on-PATH report
gaps (0 real mismatches), and suite 6's exit is a required-failure flag whose
BLOCKED rows are an external toolchain blocker.

## 2. Suite 1 — libc — exit 0

Final line (verbatim):

```
libc tests: 109 passed, 0 failed
```

All per-test lines were `ok test_*.c`; no `FAIL` line appeared. Clean pass.

## 3. Suite 2 — CRT unit — exit 0

The runner emits only one line; final line (verbatim):

```
crt unit tests: 85 passed, 0 failed
```

Clean pass; no `FAIL` line appeared.

## 4. Suite 3 — stdlib method tests — exit 0

Final line (verbatim):

```
aggregate: ok (518 methods passed, 0 skipped)
```

Per-group table emitted by the runner (verbatim):

| group | total methods | passed | failed | skipped |
|---|---:|---:|---:|---:|
| alloc_mem | 87 | 87 | 0 | 0 |
| collections | 63 | 63 | 0 | 0 |
| file_io | 28 | 28 | 0 | 0 |
| json_log_time | 56 | 56 | 0 | 0 |
| multimedia | 127 | 127 | 0 | 0 |
| net_http | 73 | 73 | 0 | 0 |
| str_vec | 47 | 47 | 0 | 0 |
| thread_async | 37 | 37 | 0 | 0 |
| **TOTAL (8 groups)** | **518** | **518** | **0** | **0** |

Clean pass.

## 5. Suite 4 — Rust examples — exit 1 (two environment variants)

### 5a. Canonical restricted PATH (the invocation used for the main table)

Command: the §0 environment with
`PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin`.

Exit: **1**. Final line (verbatim): `RESULT: FAIL`.

Key lines (verbatim):

```
WARN: [json_log] final-line counts inconsistent: cases=6 but matched+mismatched+blocked=18 (matched+mismatched=0)
TOTAL               21      6        0           0       20 FAIL_GROUPS=6      -
groups run: 10 | blocked (reported, NOT passes): 20 | real mismatches: 0 | failing groups: 6
RESULT: FAIL
```

The 6 failing groups are all `NO_REPORT` (no `RUST_EXAMPLES` final line):

| group | exit | root cause in its child log |
|---|---:|---|
| alloc | 2 | `timeout: exec(rustc): No such file or directory` |
| runtime_diag | 2 | `FATAL Rust build failed/timed out: timeout: exec(rustc): No such file or directory` |
| safecrt_memstr | 2 | `compare.sh: line 119: rustc: command not found` |
| search | 1 | `ERROR: rustc not found: rustc` |
| stdc_compare | 2 | `compare.sh: line 118: rustc: command not found` |
| threading | 2 | `compare.sh: line 74: rustc: command not found` |

`rustc` lives at `/Users/haseeb-mir/.cargo/bin/rustc`, which the canonical
restricted PATH deliberately excludes. `benchmarks/rust_examples/run_all.sh`
builds its own child PATH from `command -v` on the **caller's** PATH (header,
lines 27-38); with `~/.cargo/bin` absent from the caller PATH, the child cannot
find rustc. The 4 groups that did report (`format`, `io`, `json_log`,
`vendored_v`) all exited 0 with `PASS_WITH_BLOCKS` / `PASS`; `json_log` also
emitted the `WARN: ... counts inconsistent` line above. **Real mismatches: 0.**
This exit 1 is a missing-tool-on-PATH report gap, not a stdlib failure, and is
not counted as green.

### 5b. Toolchain-inclusive restricted PATH (diagnostic re-run, NOT the main table)

To separate the PATH artifact from actual suite behaviour, suite 4 was re-run
with a still-restricted caller PATH that additionally includes the toolchain
dirs the runner documents for reproduction
(`/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:$HOME/.cargo/bin:/Library/Frameworks/Python.framework/Versions/3.12/bin:/usr/local/bin`).
Child PATH became
`/usr/bin:/bin:/Users/haseeb-mir/.cargo/bin:/usr/bin:/usr/local/bin`.

Exit: **0**. Final line (verbatim): `RESULT: PASS`.

```
TOTAL              143    598      632           0       29 FAIL_GROUPS=0      -
groups run: 10 | blocked (reported, NOT passes): 29 | real mismatches: 0 | failing groups: 0
RESULT: PASS
```

All 10 groups reported; BLOCKED breakdown: alloc 24, runtime_diag 2, search 1,
threading 2 = 29. **0 BAD_REPORT, 0 real mismatches.**

Note on the "29 BLOCKED + 2 BAD_REPORT" expectation: that figure appears in
older branch docs (`docs/FINAL_EXPANDED_DELIVERY.md`,
`benchmarks/rust_examples/RESULTS.md`). The 2 formerly-`BAD_REPORT` groups
(`safecrt_memstr`, `vendored_v`) were fixed on the current branch — both now
emit the full 6-field contract (trailing `0` blocked field), e.g.
`RUST_EXAMPLES safecrt_memstr 21 126 126 0 0` and
`RUST_EXAMPLES vendored_v 26 156 156 0 0`. Therefore the current branch yields
29 BLOCKED with **0 BAD_REPORT**, not 2, once rustc is discoverable. The main
table still reports the canonical-PATH result (exit 1) because that is the
environment specified for this verification; variant 5b is included for
transparency and must not be used to claim the canonical run was green.

## 6. Suite 5 — adversarial_1000 — exit 0

Last literal line of the log (verbatim):

```
adv1000-agg: logs: /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/adversarial_1000/<group>.log ; final lines: /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/adversarial_1000/<group>.final
```

Terminal summary line (the one quoted in the §1 table):

```
adv1000-agg: groups=8 scenarios=8000 (target 9000) refused=5209 failures=0 hard_fail=0
```

Per-group table (verbatim):

```
group         scenarios   refused   integrity_ok  failures
------------ ---------- --------- -------------- ---------
alloc              1000       644      1000/1000         0
containers         1000       614           1000         0
fmt_io             1000       654           1000         0
json_log           1000       626           1000         0
memstr             1000       541           1000         0
multimedia         1000       720    sentinel_ok         0
net_http           1000       660           1000         0
thread_async       1000       750           1000         0
TOTAL              8000      5209              -         0
```

Target-count caveat: the script declares target 9000 but discovers 8 groups x
1000 = **8000** scenarios, so the summary literally reads `scenarios=8000
(target 9000)`. This is a counting/target mismatch in the runner, not a failed
scenario: `failures=0`, `hard_fail=0`. Do not report "9000 scenarios" — the
runner executed 8000.

## 7. Suite 6 — safety_compare — exit 1

Final line (verbatim):

```
note:    blocked rows are external-toolchain blockers (CPLUS_EXIT_ARITHMETIC), not passes
```

Terminal counts line (verbatim):

```
counts:  ok=9 blocked=5 fail=6 required_failures=1
```

`benchmarks/safety_compare/results/summary.json` confirms
`"counts": {"ok": 9, "blocked": 5, "fail": 6}`,
`"required_failures": 1`, `"status": "FAIL"`.

Interpretation, from the runner's own logic (lines 299-317, 378, 484-491):

- The **5 BLOCKED** rows (`cplus_safe` builds for alloc_bench, bench_modern,
  modern, safety_matrix/matrix and the `validate_results/.../evidence.csv`
  row) all fail the build with `CPLUS_EXIT_ARITHMETIC` /
  `failed to compile instrumented C+ runtime`. The runner classifies exactly
  these as `blocked` and explicitly prints that they are "not passes". This is
  the same external runtime-header/toolchain skew documented in
  `docs/stdlib/AGENT1_EXTERNAL_RUNTIME_BLOCKER.md` and
  `benchmarks/safety_compare/METHODOLOGY.md`.
- The **6 `fail`** rows are all `rust rustc_O` build `0/7` failures (rustc not
  found under the restricted PATH). Because their build errors are *not*
  `CPLUS_EXIT_ARITHMETIC`, the runner marks them `fail` and sets
  `REQUIRED_FAIL=1`, which is what drives the exit 1.
- The 9 `ok` rows are genuine passes (clang_O2 C rows and the `cplus_legacy`
  C+ rows), with real checksums/timings in `results/table.txt`.

So suite 6 exit 1 = one required-failure flag from missing rustc, with the
BLOCKED rows coming from the external `CPLUS_EXIT_ARITHMETIC` blocker. Neither
category is a pass.

## 8. Scope / integrity notes

- No source file was created, edited or deleted by this task. The only authored
  file is this document. No `git add` / `git commit` was performed.
- The runners write their own generated artifacts by design (e.g. `build/*`,
  `benchmarks/safety_compare/results/*`, temp logs). Those are runner
  side-effects, not authored changes; other modifications visible in
  `git status` belong to the parallel agents.
- Do not read any of the following as green: suite 4's `RESULT: FAIL` with 6
  `NO_REPORT` groups / 20 BLOCKED rows under the canonical PATH; suite 6's
  exit 1 with `required_failures=1`, its 6 rust `fail` rows, or its 5
  `CPLUS_EXIT_ARITHMETIC` BLOCKED rows; or suite 5's 9000 target, which
  actually executed 8000 scenarios. The unambiguous greens are suites 1, 2, 3
  and 5, plus the explicitly-labelled diagnostic variant §5b (which does not
  override the canonical suite-4 result).
