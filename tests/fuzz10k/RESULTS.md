# FUZZ10K — aggregate runner results

This file records the **actual output of a real run** of
`tests/fuzz10k/run_all.sh` on branch `agent/modern-stdlib`. Nothing here is
inferred from a build alone and no pass is fabricated. At the time of this run
all nine group runners had landed, so no group is marked `PENDING` — see the
scope note if that ever changes.

## Verification setup

| Field | Value |
|---|---|
| Syntax check | `sh -n tests/fuzz10k/run_all.sh` — clean (exit 0) |
| Command | `sh tests/fuzz10k/run_all.sh` (from repository root) |
| Wall clock | recorded 2026-11-23 06:49–06:53 UTC (9 group runners, sequential) |
| Branch / HEAD | `agent/modern-stdlib` / `49aa068` |
| Per-group bound | `timeout -k 10 900` (hard 900 s; a timeout is a failure, never a pass) |
| Environment | `PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:/usr/local/bin`, `CPLUS_CLANG=/usr/bin/clang`, `LC_ALL=C` |
| Exit code | `0` |
| Captured output | `build/fuzz10k/aggregate.stdout`, `aggregate.stderr` (gitignored via `build/`) |
| Per-group evidence | `build/fuzz10k/<group>.log` (full stdout+stderr), `<group>.final` (exact canonical final line) |

`aggregate.stderr` was **empty** for this run.

## Exact runner output (verbatim stdout)

```text

fuzz10k aggregate: 9 group(s), timeout=900s, PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:/usr/local/bin, CPLUS_CLANG=/usr/bin/clang

group         scenarios  crashes  hangs corruptions  refusals status
------------ ---------- -------- ------ ----------- --------- ------
alloc             10000        0      0           0     35988 PASS
containers        10010        0      0           0      6682 PASS
format            10000        0      0           0      2187 PASS
io                12000        0      0           0      9116 PASS
json_log          10000        0      0           0      4168 PASS
memstr            10240        0      0           0      5741 PASS
net_http          10000        0      0           0      6027 PASS
thread_async      12000        0      0           0      8551 PASS
vendored          13000        0      0           0      7175 PASS
TOTAL             97250        0      0           0     85635 -

fuzz10k-agg: groups=9 scenarios=97250 (target >=90000) crashes=0 hangs=0 corruptions=0 refusals=85635 hard_fail=0
fuzz10k-agg: logs: /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/fuzz10k/<group>.log ; final lines: /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/fuzz10k/<group>.final
```

## Final table

| group | scenarios | crashes | hangs | corruptions | refusals | status |
|---|---:|---:|---:|---:|---:|---|
| alloc | 10000 | 0 | 0 | 0 | 35988 | PASS |
| containers | 10010 | 0 | 0 | 0 | 6682 | PASS |
| format | 10000 | 0 | 0 | 0 | 2187 | PASS |
| io | 12000 | 0 | 0 | 0 | 9116 | PASS |
| json_log | 10000 | 0 | 0 | 0 | 4168 | PASS |
| memstr | 10240 | 0 | 0 | 0 | 5741 | PASS |
| net_http | 10000 | 0 | 0 | 0 | 6027 | PASS |
| thread_async | 12000 | 0 | 0 | 0 | 8551 | PASS |
| vendored | 13000 | 0 | 0 | 0 | 7175 | PASS |
| **TOTAL** | **97250** | **0** | **0** | **0** | **85635** | — |

Status `PASS` requires: well-formed canonical final line, runner exit 0,
`scenarios >= 10000`, and `crashes = hangs = corruptions = 0`. Refusals are
expected negative-control outcomes and never fail a group.

## Headline counts

| Metric | Observed | Target |
|---|---:|---:|
| Groups discovered at run time | 9 | 9 |
| Scenarios executed | **97250** | ≥ 90000 (9 × 10000) |
| Crashes | **0** | 0 |
| Hangs | **0** | 0 |
| Corruptions | **0** | 0 |
| Refusals (expected) | 85635 | — |
| Aggregate exit code | 0 | 0 |

Nine groups ran; five exceed the 10,000 minimum (containers 10,010,
io 12,000, memstr 10,240, thread_async 12,000, vendored 13,000), so the
campaign total **97,250** comfortably clears the ≥90,000 target.

## Toolchains

| Field | Value |
|---|---|
| OS / arch | macOS 27.2.1 (build 26B131), `arm64`, Darwin 27.2.0 |
| Backend compiler | `/usr/bin/clang` — Apple clang version 16.0.0 (clang-1600.0.26.6), target `arm64-apple-darwin27.2.0` |
| C+ driver | `<root>/build/c+`, `--strict --sanitize=none` (net_http uses `--default --sanitize=none -lcurl`) |
| C groups | C11 / C99 builds with `/usr/bin/clang`, linking `build/safe/libcplus_safecrt.a` + `build/libc/libcplus_hardened.a` |
| Sanitizers | none (ASan unsupported; canonical UBSan blocked by external `CPLUS_EXIT_ARITHMETIC` — see `FUZZ_ANALYSIS.md` §5) |

## Exact captured final lines

```text
FUZZ10K alloc 10000 0 0 0 35988
FUZZ10K containers 10010 0 0 0 6682
FUZZ10K format 10000 0 0 0 2187
FUZZ10K io 12000 0 0 0 9116
FUZZ10K json_log 10000 0 0 0 4168
FUZZ10K memstr 10240 0 0 0 5741
FUZZ10K net_http 10000 0 0 0 6027
FUZZ10K thread_async 12000 0 0 0 8551
FUZZ10K vendored 13000 0 0 0 7175
```

(Stored verbatim in `build/fuzz10k/<group>.final`.)

## Scenario-log digests (computed by each group, from the real run)

| Group | Digest record | SHA-256 / value |
|---|---|---|
| alloc | `alloc/results/scenarios.log.sha256` | `0d4ef528a2462a1c367350ce68894f76e035ec7d9c6799c14e2f478e210f4581` |
| containers | `containers/results/digest.txt` | `ba0f1af383b93e42c5af6aabb3c196a8b758b87d90a0c52a34a1ad182cab1ded` |
| format | `format/digest.txt` | `48fcdddf7e8592d224d4cf86a100753b997a0dcdc1dd1e85340584d3c06bb92b` |
| io | `io/results/scenarios.log.sha256` | `ee083050e118487c1a9fcc845ec5d3ee1bac2786796a7980e2b228e9467c2ddc` |
| json_log | `json_log/results/digests.txt` | canonical stdout `f77db2e7886f189180a8bbb0cf233c4c8a4a2f5706176756980c640175ee8f72` (both runs identical); timestamped sink logs differ by design |
| memstr | `memstr/results/scenarios.sha256` | `64e594d639d2506219621128fa9fa2a81a992dfeac29d0c9a9c6f715ba633ac9` |
| net_http | `net_http/results/digest.sha256` | `58dca5e09cbf0edcc2dcdf3c0205719c86d80bdc56e47821e611ee675ca53a46` |
| thread_async | runner stdout trailer | `FUZZ10K_DIGEST fbc723f3b4a63067` |
| vendored | `vendored/digest.txt` | `digest=a5f9a37fc06413a9`, `seed=0xc0ffee5af10d5eed`, `functions=26` |

Digests are per-group artifacts produced by the group runners; they are listed
here for audit, not re-derived by the aggregate runner. The two consecutive
aggregate runs recorded identical per-group counts, consistent with the
fixed-seed determinism each group documents in its own `NOTES.md`.

## Reproduction commands

From the repository root (no `git add`/`git commit`, no source edits; all fresh
artifacts are under gitignored `build/` and the groups' gitignored `results/`):

```sh
# 0. syntax check
sh -n tests/fuzz10k/run_all.sh

# 1. real aggregate run (requires pre-existing build/c+, build/safe/libcplus_safecrt.a,
#    build/libc/libcplus_hardened.a and, for net_http, -lcurl)
mkdir -p build/fuzz10k
sh tests/fuzz10k/run_all.sh \
    > build/fuzz10k/aggregate.stdout \
    2> build/fuzz10k/aggregate.stderr
echo $?    # 0 when every discovered group passes

# 2. inspect captured evidence
cat build/fuzz10k/aggregate.stdout
for f in build/fuzz10k/*.final; do printf '%s: ' "$(basename "$f")"; cat "$f"; done

# 3. re-run a single group directly (examples)
sh tests/fuzz10k/alloc/run.sh
sh tests/fuzz10k/net_http/run.sh
```

## Scope note — no fabrication

At the time of the recorded run exactly nine `<group>/run.sh` files existed and
all nine are in the table above; no group is `PENDING`. The campaign target is
≥90,000 = 9 × 10,000. Because discovery is dynamic, if a runner is ever absent
the moment it lands will be picked up automatically on re-run; conversely a
missing runner produces no row and the aggregate reports `groups < 9`. Do not
hand-edit the numbers above — re-run `sh tests/fuzz10k/run_all.sh`.

## Interpretation

These are **runtime observations of defined refusal/result contracts over
fixed, deterministic inputs**, not a proof of full memory safety. See
`FUZZ_ANALYSIS.md` for the consolidated interpretation of hostile-family
coverage, crash/hang/corruption detection, negative controls, the explicit
evidence-vs-proof boundary, sanitizer limits and host scope.
