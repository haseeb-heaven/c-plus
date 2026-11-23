# Legacy ABI labs — unified runner evidence

Owner: Task 4.  Files authored: `tests/abi/run_legacy_labs.sh` (executable) and
this document.  No other file was created, edited, staged or committed.  In
particular, no lab source, lab runner, compiler, runtime or stdlib file was
touched.  All generated output is under `build/abi-labs/`.

---

## 1. What the runner does

`tests/abi/run_legacy_labs.sh` discovers labs **at run time** (no hard-coded lab
list) under five groups and executes each under a bounded wall-clock timeout:

| group | discovered root | discovery result |
| --- | --- | --- |
| `legacy_c` | `tests/abi/legacy_c` | 50 standalone `.c` labs (`lc01`…`lc50`), one `main` per file |
| `c_to_cp` | `tests/abi/c_to_cp` | 1 lab: `run.sh` |
| `cp_to_c` | `tests/abi/cp_to_c` | 1 lab: `run_cp_to_c.sh` |
| `precompiled` | `tests/abi/precompiled` | 1 lab: `run_precompiled_lab.sh` |
| `abi_compare` | `tools/abi_compare` | 1 lab: `run.sh` (also accepts `tools/abi_compare.sh` and a single-file `tools/abi_compare`) |

Discovery rules (deterministic, sorted by shell glob order):

1. A group-level `run.sh` / `run_all.sh` / `run_tests.sh` / `Makefile` or
   `run_*.sh` / `run_*.py` is the group's authoritative lab.
2. Immediate lab subdirectories are inspected the same way; a subdirectory
   runner wins over sources inside that subdirectory.
3. Loose `*.c` / `*.cp` / `*.cpp` files become compile-and-run labs **only when
   no runner was discovered for that group**.  This is the `legacy_c` case.  It
   prevents component files (`c_to_cp_lib.c`, `precomp_app.cp`, …) from being
   mis-executed as standalone labs.
4. `_`-prefixed scratch/probe files and `artifacts/`, `build/`, `out/`,
   `output/`, `logs/`, `results/`, `.out/`, `__pycache__/` and similar generated
   directories are ignored.  Target-path deduplication prevents double entries.
5. A group with zero discovered labs is **not** silently green: it produces a
   `SKIPPED` `<group>::EMPTY` row and makes the runner exit nonzero.  A filter
   matching nothing produces `all::NO_MATCH` with the same effect.

Execution model:

* `src_c` → `/usr/bin/clang -O1 -std=gnu11 -Wall -Wextra` with the repo's
  `libc`/`safe`/`safeclib` include paths and the two prebuilt static archives,
  then a bounded run of the produced binary.
* `src_cp` → `./build/c+ build --sanitize=none`, then a bounded run.  Missing
  `build/c+` yields `BLOCKED`, never a silent pass.
* `src_cpp` → `clang++ -std=c++17`; `script_sh` → `sh <runner>`, `script_py` →
  `python3 <runner>`, `script_exec` → direct exec, `make` → `make -C <dir>`.
* Labs may self-classify by writing a status token to the file named in
  `$ABI_LAB_STATUS_FILE` (`PASS|FAIL|BUILD_FAILURE|TIMEOUT|SKIPPED|BLOCKED`);
  an outer timeout always wins over a self-report.

Classification and exit contract:

| status | meaning | exits nonzero? |
| --- | --- | --- |
| `PASS` | exited 0 | no |
| `FAIL` | ran, exited nonzero | **yes** |
| `BUILD_FAILURE` | compile / `make` step exited nonzero | **yes** |
| `TIMEOUT` | bounded run exited 124 or 137 | **yes** |
| `SKIPPED` | self-reported or no match / empty group | yes when it is an empty/no-match row |
| `BLOCKED` | missing prerequisite (compiler, `build/c+`, timeout utility) | no (reported) |

There is no `|| true` masking.  Every lab runs even after earlier failures.
Timeouts: build 300 s, run 120 s by default; overridable with
`ABI_LAB_BUILD_TIMEOUT`, `ABI_LAB_RUN_TIMEOUT` or `--timeout N`.  Cleanup uses
`timeout` (here `/usr/bin/timeout`, which returns 124 on expiry).

Filters: `--only NAME`, `--group NAME`, `--only=NAME`, `--group=NAME`,
repeatable and comma-separated; matching is substring-based on the full lab
name (`group/lab`) or on the group name.  `--list` prints discovery without
running.  `--help` prints usage; unknown options exit 2.

---

## 2. Exact validation commands and results

All commands were run from the repository root on branch `agent/modern-stdlib`,
`arm64-apple-darwin`, Apple clang 16, `CPLUS_CLANG=/usr/bin/clang`.

| # | command | exit code | observed result |
| --- | --- | ---: | --- |
| V1 | `bash -n tests/abi/run_legacy_labs.sh` | 0 | no syntax errors |
| V2 | `bash tests/abi/run_legacy_labs.sh --list` | 0 | `discovered 54 lab(s); selected 54 lab(s)`; per-group counts 50/1/1/1/1 |
| V3 | `bash tests/abi/run_legacy_labs.sh` | 0 | `total=54 pass=54 fail=0 build_failure=0 timeout=0 skipped=0 blocked=0`, `required_failures=0` |
| V4 | `bash tests/abi/run_legacy_labs.sh --only lc01` | 0 | 1 lab selected and passed (`legacy_c/lc01_primitives`) |
| V5 | `bash tests/abi/run_legacy_labs.sh --group precompiled` | 0 | 1 lab selected and passed (`precompiled/precompiled_lab`) |
| V6 | `bash tests/abi/run_legacy_labs.sh --only zzz_no_such_lab` | 1 | `all::NO_MATCH … SKIPPED`, `required_failures=1` |
| V7 | `bash tests/abi/run_legacy_labs.sh --group legacy_c --only zzz` | 1 | `all::NO_MATCH … SKIPPED`, `required_failures=1` |
| V8 | `bash tests/abi/run_legacy_labs.sh --list --only lc01,lc02` | 0 | 2 labs selected (comma-separated `--only`) |
| V9 | `bash tests/abi/run_legacy_labs.sh --list --group legacy_c,abi_compare` | 0 | 51 labs selected (50 + 1) |
| V10 | `bash tests/abi/run_legacy_labs.sh --list --only=lc01 --group=legacy_c` | 0 | 1 lab selected |
| V11 | `python3 -m json.tool build/abi-labs/summary.json` | 0 | full-run, no-match and final summaries all valid JSON |
| V12 | `grep -n '|| true' tests/abi/run_legacy_labs.sh` | 1 | only the comment documenting the prohibition; no executable `|| true` |
| V13 | `wc -l build/abi-labs/results.jsonl` | 0 | 54 records |

`python3 -m json.tool` is used only for offline verification of the generated
JSON; the runner itself emits JSON with pure shell `printf` (no Python in the
correctness path), matching `tools/run_final_evidence.sh`.

### Authoritative run (V3, final re-run at `2026-11-23T04:04:36Z`)

```
unified ABI lab runner: branch=agent/modern-stdlib started=2026-11-23T04:04:36Z
discovered=54 selected=54  build_timeout=300s run_timeout=120s
evidence: /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/abi-labs
...
run_legacy_labs: total=54 pass=54 fail=0 build_failure=0 timeout=0 skipped=0 blocked=0
required_failures=0  summary=/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/abi-labs/summary.json  logs=/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/abi-labs/logs
```

All 54 labs reported `exit=0`.  Per-lab final lines captured verbatim under
`build/abi-labs/logs/*.stdout` include:

| lab | captured final line |
| --- | --- |
| `legacy_c/lc01_primitives` | `lc01_primitives: OK sizes=1/2/4/8/8 calc=122222` |
| `legacy_c/lc50_state_machine` | `lc50 result: OK accepted=4 current=42 trans=4 history=SARA` |
| `c_to_cp` | `c_to_cp: 24 passed, 0 failed` |
| `cp_to_c/cp_to_c` | `cp_to_c lab: 37 passed, 0 failed` |
| `precompiled/precompiled_lab` | `precompiled_lab: 3 passed, 0 failed` |
| `abi_compare` | `abi_fp: capture complete. baseline candidates:` |

### Honest note on the first exploratory run

Before the parallel lab authors finished writing their files, an earlier run
reported 51 PASS / 3 FAIL (`lc37` rc 127, `c_to_cp` rc 1, `precompiled_lab` rc 1)
with stray line fragments in the captured stdout.  Those files were being
rewritten concurrently; direct re-execution of the already-built `lc37` binary
returned 0 and printed `lc37 result: OK records=3 id3=3 value3=3.5`, and a clean
re-run after the writers finished produced the 54/54 result above.  The
runner's behaviour was correct (it captured and reported the observed nonzero
codes rather than masking them).  Concurrent invocations of this runner share
`build/abi-labs/` and can interfere with one another; run one instance at a
time for authoritative evidence.

---

## 3. Generated evidence inventory

| path | contents |
| --- | --- |
| `build/abi-labs/summary.json` | schema 1: branch, start/finish UTC, filters, timeouts, per-group counts, aggregate counts, `required_failures`, and one object per lab with exact command, exit code, duration and stdout/stderr paths |
| `build/abi-labs/results.jsonl` | one pipe-delimited result record per lab (54 lines) |
| `build/abi-labs/commands.log` | exact description and `rc`/`status` per lab |
| `build/abi-labs/logs/<lab>.stdout` | verbatim stdout of each lab (216 files: 54 stdout + 54 stderr + 54 build.stdout + 54 build.stderr) |
| `build/abi-labs/status/<lab>.status` | optional self-classification file consumed from `$ABI_LAB_STATUS_FILE` |
| `build/abi-labs/artifacts/<lab>.bin` | binaries produced by the generic compile-and-run harness |

Final `summary.json` counts:

```json
"counts": {"total": 54, "pass": 54, "fail": 0, "build_failure": 0, "timeout": 0, "skipped": 0, "blocked": 0},
"required_failures": 0
```

Exact bytes are in `build/abi-labs/summary.json`, validated by
`python3 -m json.tool`.

---

## 4. Scope / what this does not claim

* The runner reports what each lab itself checks; it does not independently
  re-derive ABI facts.  Lab-internal counts (24, 37, 3 checks) come from the lab
  runners' own output.
* A `PASS` means the lab's process exited 0.  `BUILD_FAILURE` and `FAIL` are
  kept distinct by which phase returned nonzero.
* `BLOCKED` is reported but does not flip the exit status, mirroring
  `tools/run_final_evidence.sh`; empty groups and no-match filters deliberately
  do flip it so an empty run can never look green.
* No live-game, Windows x86 or MSVC claim is made.  Evidence is for the host
  `arm64-apple-darwin` toolchain and the supported `./build/c+` driver path.
