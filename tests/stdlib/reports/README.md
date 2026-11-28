# Adversarial runner integration review — exact commands and results

Reviewer: parallel integration agent (file ownership limited to
`tests/stdlib/run_adversarial.py`, `tests/stdlib/run_adversarial.sh`, and this
file). Date: 2026-11-23. Branch: `agent/modern-stdlib`; review began at HEAD
`ed2ac0f` and a parallel commit moved HEAD to `9a061ae` mid-review (see §3).

This file records what was verified, which defects were demonstrated and fixed,
and what limitations remain. It does **not** claim that passing adversarial
tests prove 100% library safety.

## 1. Files changed by this review

| File | Change |
|---|---|
| `tests/stdlib/run_adversarial.py` | Fixed vacuous-pass on empty discovery; reject unknown suite/mode; authoritative timeout flag instead of exit-code 124; handle `OSError` as a FAIL record; timeout the `git rev-parse` subprocess; clear stale logs/executables before each record; record `timed_out` and `stdin_fixture`; derive non-zero exit by subtracting only PASS/SKIPPED (BLOCKED remains a failure) |
| `tests/stdlib/run_adversarial.sh` | Comment wording only (removed a literal failure-suppression phrase so greps are unambiguous); `set -eu` + `exec python3 ...` unchanged |
| `tests/stdlib/reports/README.md` | New (this file) |

Executing the required commands regenerated the **generated** artifacts
`reports/adversarial.json`, `reports/adversarial.txt` and `reports/logs/**`.
No header, test source, fixture, or other repository file was edited. No
`git add`/`git commit` was performed.

## 2. Defects demonstrated and fixed

All three defects were reproduced against the pre-fix runner, then re-run
against the fixed runner.

### D1 — vacuous success when no tests are discovered (honesty defect)

Pre-fix: an unknown `--suite` (or an `--only` matching nothing) produced an
empty green summary and exit 0:

```
$ sh tests/stdlib/run_adversarial.sh --suite definitely-not-a-suite
Adversarial stdlib summary (ed2ac0f) suite=definitely-not-a-suite
modes=strict-none,default compiler=/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/c+
SUMMARY  TOTAL=0
EXIT=0
```

Post-fix (same command, exit 2, no report written):

```
error: unknown suite 'definitely-not-a-suite' (expected one of agent2,all,deep,fuzz,regressions)
EXIT=2
```

Also fixed for `--only no-such-test`:

```
error: no tests discovered (suite=regressions only='no-such-test'); refusing to report a vacuous pass
EXIT=2
```

### D2 — unknown mode silently built with default flags but labelled as the requested mode

Pre-fix: `--mode bogus` produced `PASS` and a JSON record with `mode='bogus'`
whose `build_command` contained **no mode flags at all** (i.e. the compiler's
default behaviour). The report therefore claimed a mode that was never
requested of the compiler.

Post-fix:

```
$ sh tests/stdlib/run_adversarial.sh --suite regressions --mode bogus --only adv_integer_bounds
error: unknown mode in ['bogus'] (expected one of asan,debug,default,release,strict-none,ubsan)
EXIT=2
```

### D3 — legitimate exit code 124 mislabelled as TIMEOUT

Pre-fix, isolated reproduction of the runner's `run()` and classification
logic:

```
program exiting 124 -> rc= 124 classified as TIMEOUT (expected PASS)
```

The old code used the numeric value 124 as its timeout sentinel, so any test
that legitimately returns 124 (with a matching `.exit` fixture) would be a
false failure. `run()` now returns an explicit `timed_out` flag; 124 remains
the documented JSON `runtime_exit` for a real timeout, but classification uses
the flag.

### Additional hardening (demonstrable by inspection)

- The metadata subprocess `git rev-parse --short HEAD` had **no timeout**,
  unlike every build and test run. It now uses a 10 s timeout and records
  `head: "unknown"` on failure.
- `subprocess.run` could raise `OSError` (missing compiler/tool) and abort the
  whole runner with no JSON or summary. It is now captured into a BUILD_FAILURE
  / FAIL record so a report is always produced.
- `build.log`, `.stdout`, `.stderr` and the executable are cleared at the start
  of every record. Previously a SKIPPED libFuzzer target could retain a stale
  `.build.log` from an earlier real build (observed:
  `logs/strict-none/fuzz_json.build.log` dated 06:52 while the JSON said
  SKIPPED). SKIPPED records now write an explicit `SKIPPED: ...` marker.
- Final exit status is derived as "every classification except PASS and SKIPPED
  is a failure". This preserves the original contract that BLOCKED
  (sanitizer unsupported by backend) is a failure, while an unexpected new
  class cannot silently turn a run green.

## 3. Required commands — exact output

Both commands were run from
`/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib` after the fixes, at HEAD
`ed2ac0f`. A parallel integration agent committed `9a061ae` ("Wire new
C23/atomic sources into builds and fix aligned allocation tracking") during
this review; re-running both commands at `9a061ae` produced byte-identical
results apart from the `HEAD` token in the banner (`PASS=10 TOTAL=10` and
`PASS=20 TOTAL=20` respectively, both exit 0). The blocks below are the exact
captured output at `ed2ac0f`.

### Command A

```
sh tests/stdlib/run_adversarial.sh --suite regressions --mode strict-none
```

Exit status: **0**. Exact stdout:

```
Adversarial stdlib summary (ed2ac0f) suite=regressions
modes=strict-none compiler=/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/c+
adv_alloc_failure                          strict-none  PASS        
adv_containers                             strict-none  PASS        
adv_exact_fit                              strict-none  PASS        
adv_integer_bounds                         strict-none  PASS        
adv_json_fs_net                            strict-none  PASS        
adv_lifecycle                              strict-none  PASS        
adv_long_input                             strict-none  PASS        
adv_null_zero                              strict-none  PASS        
adv_stale_views                            strict-none  PASS        
adv_thread_contention                      strict-none  PASS        
SUMMARY PASS=10 TOTAL=10
```

### Command B (2-mode subset)

```
sh tests/stdlib/run_adversarial.sh --suite regressions --mode strict-none,default
```

Exit status: **0**. Exact stdout:

```
Adversarial stdlib summary (ed2ac0f) suite=regressions
modes=strict-none,default compiler=/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/c+
adv_alloc_failure                          strict-none  PASS        
adv_containers                             strict-none  PASS        
adv_exact_fit                              strict-none  PASS        
adv_integer_bounds                         strict-none  PASS        
adv_json_fs_net                            strict-none  PASS        
adv_lifecycle                              strict-none  PASS        
adv_long_input                             strict-none  PASS        
adv_null_zero                              strict-none  PASS        
adv_stale_views                            strict-none  PASS        
adv_thread_contention                      strict-none  PASS        
adv_alloc_failure                          default      PASS        
adv_containers                             default      PASS        
adv_exact_fit                              default      PASS        
adv_integer_bounds                         default      PASS        
adv_json_fs_net                            default      PASS        
adv_lifecycle                              default      PASS        
adv_long_input                             default      PASS        
adv_null_zero                              default      PASS        
adv_stale_views                            default      PASS        
adv_thread_contention                      default      PASS        
SUMMARY PASS=20 TOTAL=20
```

(Line ordering above follows the runner's per-mode loop: all ten `strict-none`
records first, then all ten `default` records.)

## 4. Verification checklist

| Requirement | Result / evidence |
|---|---|
| No failure-suppressing shell no-op in runner files | `grep -nE '\|\|[[:space:]]*true' tests/stdlib/run_adversarial.py tests/stdlib/run_adversarial.sh` → no matches. The shell wrapper uses `set -eu` and `exec`. |
| All builds/runs bounded by timeout | Build: 90 s, runtime: 30 s, metadata `git`: 10 s (after fix). Every `subprocess.run` in the file now passes a `timeout`. |
| Exact exit-code checks | `build_exit != 0` → BUILD_FAILURE/BLOCKED; `runtime_exit != expected_exit` → FAIL; `.exit` fixture honoured (default 0). |
| stdin fixtures supplied | `sh ... --suite deep --mode strict-none --only deep_io_1byte` → `PASS`, JSON `stdin_fixture="tests/stdlib/deep_io_1byte.stdin"`. |
| stdout/stderr expectation checks | Same run: `expectation_files=["tests/stdlib/deep_io_1byte.expected", "tests/stdlib/deep_io_1byte.stderr.expected"]`, byte-exact comparison, PASS. |
| SKIPPED libFuzzer rule sound | Only `fuzz_containers.cp`, `fuzz_json.cp`, `fuzz_string.cp` in `tests/stdlib/fuzz/` contain `LLVMFuzzerTestOneInput` with no `main`; no `f100_*.c` in `tests/crt/fuzz/` does. All three are exactly the targets driven by the existing `tests/stdlib/fuzz/run_fuzz.sh`, so the skip reason is accurate. Verified `sh ... --suite fuzz --mode strict-none --only fuzz_json` → SKIPPED, exit 0, JSON `build_exit=null`, log marker present. |
| Machine-readable JSON required fields | `schema_version, generated_at, head, suite, modes, fast, counts, total, results` and per-record `test, group, source, mode, build_command, build_exit, build_stderr, classification, runtime_command, runtime_exit, expected_exit, expectation_files, reason` all present in every record (checked programmatically; zero missing). Added `timed_out` and `stdin_fixture` as extra fields. |
| Final summary non-zero on failures | `bad = sum(v for k,v in counts.items() if k not in {'PASS','SKIPPED'})`; `sys.exit(1 if bad else 0)`. FAIL, BUILD_FAILURE, TIMEOUT and BLOCKED all remain failures (as before this review). Wrong suite/mode/empty selection now exits 2. |

## 5. Remaining issues / limitations (not changed)

1. `--fast` is accepted and recorded in JSON (`"fast": true`) but does not
   alter execution. It is a documented CLI option with no semantics. No change
   was made because assigning it semantics would either skip checks (weakening)
   or invent an unspecified policy. Recommend removing the flag or defining it
   explicitly.
2. `SKIPPED` (engine-driven libFuzzer target) is deliberately **not** counted
   as a failure, and is printed in the SUMMARY line. `BLOCKED` (sanitizer
   unsupported by backend) **is** counted as a failure — this preserves the
   runner's original exit-status contract. On hosts without ASan, an `asan`
   mode run will therefore exit non-zero with `BLOCKED` records.
3. `fuzz-c` builds use plain `/usr/bin/clang` and ignore the selected `mode`;
   the JSON still labels the record with the requested mode. This matches the
   documented split ("C fuzz harnesses with /usr/bin/clang") but means mode
   labels for the `fuzz-c` group are informational only.
4. Timeouts are applied with `subprocess.run(timeout=...)`, which kills the
   direct child. A grandchild that inherits the output pipe could, in theory,
   delay pipe EOF; process-group kill was not introduced because it changes
   platform behaviour and no such case was observed here.
5. PASS means the bounded build exited 0, the bounded run exited the expected
   code, and any present `.expected`/`.stderr.expected` files matched
   byte-for-byte. It is not a proof of memory safety, and no live-game or
   production claim is made.
