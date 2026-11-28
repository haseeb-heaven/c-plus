# ADV1000 — aggregate runner results

This file records the **actual output of a real run** of
`tests/adversarial_1000/run_all.sh` on branch `agent/modern-stdlib`. Nothing
here is inferred from a build alone and no pass is fabricated.

## Verification setup

| Field | Value |
|---|---|
| Syntax check | `sh -n tests/adversarial_1000/run_all.sh` — clean |
| Command | `sh tests/adversarial_1000/run_all.sh` (from repository root) |
| Wall clock | 2026-11-23 05:51:59Z → 05:52:08Z (8 group runners, sequential) |
| Per-group bound | `timeout -k 10 600` (hard 600 s; a timeout is a failure, never a pass) |
| Environment | `PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:/usr/local/bin`, `CPLUS_CLANG=/usr/bin/clang`, `LC_ALL=C` |
| Exit code | `0` |
| Captured output | `build/adversarial_1000/aggregate.stdout`, `aggregate.stderr` (both gitignored via `build/`) |
| Per-group evidence | `build/adversarial_1000/<group>.log` (full stdout+stderr), `<group>.final` (exact canonical final line) |

The runner discovers groups dynamically as immediate subdirectories containing
`NOTES.md` plus a runner (`run.sh`, `runner.sh`, `run_all.sh`, `run_1000.sh`,
`run_1000_scenarios.sh`, or a single unambiguous `*.sh`). It continues after
any failure and returns non-zero if any group: exits non-zero, emits a
missing/malformed final line, reports `scenarios != 1000`, reports any
integrity count below 1000 / a non-`*_ok` token, or reports `failures > 0`.

## Exact runner output (verbatim stdout)

```text

adversarial-1000 aggregate: 8 group(s), timeout=600s, PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:/usr/local/bin, CPLUS_CLANG=/usr/bin/clang

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

adv1000-agg: groups=8 scenarios=8000 (target 9000) refused=5209 failures=0 hard_fail=0
adv1000-agg: logs: /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/adversarial_1000/<group>.log ; final lines: /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/adversarial_1000/<group>.final
```

`aggregate.stderr` was empty for this run.

## Final table

| group | scenarios | refused | integrity_ok | failures |
|---|---:|---:|---|---:|
| alloc | 1000 | 644 | 1000/1000 | 0 |
| containers | 1000 | 614 | 1000 | 0 |
| fmt_io | 1000 | 654 | 1000 | 0 |
| json_log | 1000 | 626 | 1000 | 0 |
| memstr | 1000 | 541 | 1000 | 0 |
| multimedia | 1000 | 720 | sentinel_ok | 0 |
| net_http | 1000 | 660 | 1000 | 0 |
| thread_async | 1000 | 750 | 1000 | 0 |
| **TOTAL** | **8000** | **5209** | — | **0** |

`integrity_ok` is the per-group integrity field: for most groups it is the
**count** of scenarios whose canary/state/sentinel check passed (`1000` means
all 1000 passed); `alloc` carries two such counts (`canary_ok=1000`,
`state_ok=1000`, shown as `1000/1000`); `multimedia` emits the literal token
`sentinel_ok` instead of a count. All layouts are accepted by the runner and
explained in each group's `NOTES.md`.

## Headline counts

| Metric | Observed | Target |
|---|---:|---:|
| Groups discovered at run time | 8 | 9 |
| Scenarios executed | 8000 | 9000 |
| Refusals observed | 5209 | — |
| Failures | **0** | 0 |
| Aggregate exit code | 0 | 0 |

**Scope note — no fabrication.** At the time of this real run, exactly eight
group directories had `NOTES.md` (`alloc`, `containers`, `fmt_io`, `json_log`,
`memstr`, `multimedia`, `net_http`, `thread_async`); a ninth group directory
was not present on disk, so no ninth row is invented. The campaign target is
9000 = 9 × 1000. Because discovery is dynamic, the moment a ninth compliant
group lands, re-running the same command includes it automatically; do not
hand-edit the numbers above.

## Exact captured final lines

```text
ADV1000 alloc 1000 644 1000 1000 0
ADV1000 containers 1000 614 1000 0
ADV1000 fmt_io 1000 654 1000 0
ADV1000 json_log 1000 626 1000 0
ADV1000 memstr 1000 541 1000 0
ADV1000 multimedia 1000 720 sentinel_ok 0
ADV1000 net_http 1000 660 1000 0
ADV1000 thread_async 1000 750 1000 0
```

(Stored verbatim in `build/adversarial_1000/<group>.final`.)

## Reproduction commands

From the repository root:

```sh
# 1. syntax check
sh -n tests/adversarial_1000/run_all.sh

# 2. real aggregate run (requires pre-existing build/ artifacts: build/c+,
#    build/safe/libcplus_safecrit.a, build/libc/libcplus_hardened.a,
#    build/ext/libyyjson.a, and pkg-config on PATH)
mkdir -p build/adversarial_1000
sh tests/adversarial_1000/run_all.sh \
    > build/adversarial_1000/aggregate.stdout \
    2> build/adversarial_1000/aggregate.stderr
echo $?    # 0 when every discovered group passes

# 3. inspect captured evidence
cat build/adversarial_1000/aggregate.stdout
for f in build/adversarial_1000/*.final; do printf '%s: ' "$f"; cat "$f"; done

# 4. re-run a single group directly (example)
sh tests/adversarial_1000/alloc/run.sh
```

No `git add`, `git commit`, or source edit is performed by any of these
commands; all fresh artifacts live under the gitignored `build/` tree and the
groups' own gitignored `results/` directories.

## Interpretation

These are **runtime observations of defined refusal/result contracts over
fixed, deterministic inputs**, not a proof of full memory safety. See
`SECURITY_LOGS.md` for the per-group logging, digest, determinism and
negative-control audit and the explicit evidence-vs-proof boundary.
