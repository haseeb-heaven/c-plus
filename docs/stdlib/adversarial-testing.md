# Adversarial testing, fuzzing and regression infrastructure (Agent 2)

Date: 2026-11-23. Branch `agent/modern-stdlib`. This document records the Phase 1
audit, the dedicated runner contract, and the exact commands/results. It does
not claim that fuzzing proves safety or that all stdlib APIs are covered.

## Phase 1 — audit findings (existing infrastructure)

| Area | Finding | Evidence |
|---|---|---|
| `tests/run_tests.sh` | Runs `tests/cases/`, `tests/compile_fail/`, `tests/runtime_fail/` only. It does **not** execute `tests/stdlib/deep_*.cp`. | script lines 84–110; deep corpus is separate |
| `tests/run_all_evidence.sh` | Aggregates suites by exit code but uses `if "$@"` (correct). It does not invoke the dedicated deep runner directly; deep tests are reached via `tests/stdlib/run_deep_tests.sh` only if wired elsewhere. | `run()` helper and suite list |
| `tests/stdlib/run_all.sh` | Uses `|| fail=1` for suites (correct classification) but line 25 uses `|| printf ...` for execution coverage, which deliberately ignores failure — informational only, not a correctness gate. | lines 10–25 |
| `tests/stdlib/coverage_exec.sh` | **Not a correctness runner.** Line 50 uses `|| true`, discards stdout/stderr, and applies no timeout. A test that exits non-zero or hangs still contributes coverage. | `coverage_exec.sh:50` |
| `tests/stdlib/coverage.py` | Static `\bname\s*\(` text search; measures API-call-site reachability, not execution or safety. | `coverage.py:2–16,57–61` |
| Deep fixtures | 28 deep tests have byte-exact `.expected`/`.stderr.expected` fixtures; 5 have `.stdin`. Prior to the new runner, `tests/stdlib/run_deep_tests.sh` checked exit codes but not expected output. | `tests/stdlib/*.expected`, runner source |
| Sanitizer dependence | ASan/Leak/Thread/Memory/CFI are rejected by the backend probe on this host; only none/undefined/integer execute. No correctness suite may depend on ASan here. | `tests/stdlib/agent2/SANITIZER_MATRIX.md` |
| Strict mode | The C+ driver supports `--strict` and `--default`; several legacy tests were historically only built in default mode. The new runner always includes `--strict --sanitize=none` as the first mode. | driver `--help`, runner |
| Negative tests | Compile-time negatives live in `tests/compile_fail/` and `tests/stdlib/compile_time/`; runtime negatives in `tests/runtime_fail/`. They were not part of one classified summary before the Agent 2 runner. | runner inventories |
| Never executed | Platform modules (`async`, `audio`, `graphics`, `gui`, `http`, `image`, `input`, `video`, `window`) have static audits but no deep runtime tests; 126/381 stdlib entries have no deep-test reference. | `tests/stdlib/agent2_audit/module_audits/`, `REGRESSION_INVENTORY.md` |

No existing test was deleted to make the suite pass.

## Phase 2 — dedicated runner

`tests/stdlib/run_adversarial.sh` (thin shell wrapper) invokes
`tests/stdlib/run_adversarial.py`. It:

1. Discovers `deep_*.cp`, `tests/stdlib/agent2/*.cp`,
   `tests/stdlib/regressions/*.cp`, `tests/stdlib/fuzz/*.cp`, and
   `tests/crt/fuzz/f100_*.c`.
2. Builds each source independently with the supported compiler:
   `./build/c+ build <src> --strict --sanitize=none` (first mode), `--default`,
   `--strict --sanitize=undefined`, `--strict --sanitize=address` (BLOCKED if
   unsupported), `--debug`, `--release`; C fuzz harnesses with `/usr/bin/clang`.
3. Supplies `<name>.stdin` when present.
4. Checks `<name>.exit` (default 0), `<name>.expected` and
   `<name>.stderr.expected` byte-for-byte when present.
5. Applies 90 s build and 30 s run timeouts; 124/≥128 are TIMEOUT, never PASS.
6. Preserves `.build.log`, `.stdout`, `.stderr` under
   `tests/stdlib/reports/logs/<mode>/`.
7. Writes `tests/stdlib/reports/adversarial.json` (machine-readable) and
   `adversarial.txt` (terminal summary).
8. Classifies PASS / FAIL / BUILD_FAILURE / TIMEOUT / SKIPPED / BLOCKED and
   exits non-zero on any non-PASS. It never uses `|| true`.

Usage:

```sh
sh tests/stdlib/run_adversarial.sh --suite all --mode strict-none,default
sh tests/stdlib/run_adversarial.sh --suite regressions --only adv_
sh tests/stdlib/run_adversarial.sh --mode strict-none,ubsan
```

## Phase 3 — adversarial families

`tests/stdlib/regressions/` contains deterministic, self-checking families:
null/zero inputs, exact-fit/one-byte-too-small buffers, integer boundaries,
long inputs, lifecycle/reinit/drop, allocation failure, container growth and
collisions, stale views, thread contention, malformed JSON, filesystem errors,
and local-only network disconnects. Each prints `<tag> <0|1>` and a final
summary line, exits non-zero on any failed check, and uses bounded loops and
timeouts. Notes files record exact commands and observed results.

## Phase 4 — fuzzing

Fuzz assets are split across `tests/crt/fuzz/`, `tests/libc/`, and
`tests/stdlib/fuzz/`. The `f100_*` corpus is a numbered 100-configuration
program with fixed seeds and deterministic operation counts. Fuzzing is
reported by configuration count, seed, iteration count and duration where
recorded — never as a percentage of safety. A passing fuzz run is evidence of
the tested operation stream only.

## Phase 5 — reports and reproducibility

All reports are written under `tests/stdlib/reports/`. Re-run the exact final
suite with:

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
sh tests/stdlib/run_adversarial.sh --suite all --mode strict-none,default,ubsan
```

## Phase 6 — final evidence from the latest report

Source and provenance: `tests/stdlib/reports/adversarial.json` (`schema_version`
1, `generated_at` `2026-11-23T07:03:10.090214`, `head` `ed2ac0f`, `suite` `all`,
`modes` `strict-none,default,ubsan`, `fast` false) and its terminal companion
`tests/stdlib/reports/adversarial.txt` (final line:
`SUMMARY PASS=468 SKIPPED=9 TOTAL=477`). Every number in this section is read
from those two files; none is estimated. The JSON `counts` object contains only
`PASS` and `SKIPPED`, so "failed" and "timed-out" below mean "no record with
that classification in this report", not "failure was proven impossible".

### Final counts

Counts are **test/mode records** (159 unique sources x 3 modes), not unique sources.

| Measurement | Count | Basis |
|---|---:|---|
| Records in report (`total`) | 477 | JSON `total`; matches `len(results)` and the TXT `TOTAL` |
| Compiled (real build command issued, `build_exit = 0`) | 468 | records whose `build_command` is an actual compiler invocation |
| Executed (`runtime_exit` recorded) | 468 | records with a non-null `runtime_exit` |
| Passed (`PASS`) | 468 | JSON `counts.PASS`; all 468 have `runtime_exit = 0` |
| Failed (`FAIL` / `BUILD_FAILURE` / `BLOCKED`) | 0 | no such classification appears in the JSON |
| Skipped (`SKIPPED`) | 9 | JSON `counts.SKIPPED` |
| Timed out (`TIMEOUT`) | 0 | no TIMEOUT classification; 90 s build / 30 s run limits were configured |

The 9 SKIPPED records are `fuzz_containers`, `fuzz_json` and `fuzz_string` in
each mode. They carry a synthetic `build_command` (`libFuzzer target (no main)`),
`build_exit = 0`, no `runtime_command` and `runtime_exit = null`: they were
**neither built nor executed**. Recorded reason: `libFuzzer entry point requires
a fuzzing engine; use tests/stdlib/fuzz/run_fuzz.sh`. `fuzz_hp` is the only
non-f100 fuzz harness that was built and run.

### Modes run

| Mode key | Compiler invocation | Records | PASS | SKIPPED |
|---|---|---:|---:|---:|
| `strict-none` | `build/c+ build <src> --strict --sanitize=none` | 159 | 156 | 3 |
| `default` | `build/c+ build <src> --default` | 159 | 156 | 3 |
| `ubsan` | `build/c+ build <src> --strict --sanitize=undefined` | 159 | 156 | 3 |

No `asan`, `debug`, `release`, leak, thread, memory or CFI mode was run in this
final report. Per group (unique sources, then records): `deep` 29 sources / 87
records (all PASS), `agent2` 16 / 48 (all PASS), `regressions` 10 / 30 (all
PASS), `fuzz` 44 / 132 (123 PASS, 9 SKIPPED), `fuzz-c` 60 / 180 (all PASS).

### fuzz100 counts

The `f100_*` corpus contributes **100 unique configurations x 3 modes = 300
records, all PASS**. 40 C++ configuration names live in group `fuzz` (120
records) and 60 C configuration names in group `fuzz-c` (180 records). Each of
the 300 records has `runtime_exit = 0`. This is a count of deterministic
fixed-seed configurations executed, not a safety percentage.

### Seed / iteration information (not stored in the report)

Neither `adversarial.json` nor `adversarial.txt` records a seed, iteration count
or duration field (top-level and per-result keys were checked). Fixed seeds and
operation budgets are documented in the per-family notes and are quoted here as
documentary evidence only; they were not re-measured by this documentation or
checked by the adversarial runner:

| Configuration IDs | Notes file | Seed evidence | Operation evidence |
|---|---|---|---|
| F-001..F-020 | `tests/crt/fuzz/F100_MEMORY_STRING_NOTES.md` | seeds `0xF1000001`..`0xF1000014`, LCG `1664525`/`1013904223` mod 2^32 | 1200 iterations per config (F-020 runs 1200 twice) |
| F-021..F-040 | `tests/crt/fuzz/F100_ALLOC_LIFE_NOTES.md` | 64-bit LCG `6364136223846793005`/`1442695040888963407`, per-config `F100_SEED ^ 0x0NN` | `F100_OPS = 1200` per config |
| F-041..F-060 | `tests/crt/fuzz/F100_FORMAT_IO_NOTES.md` | format `0xF100C0DE`, I/O `0xF10010DE`; LCG `1664525`/`1013904223` mod 2^32 | 1057..5200 ops per config (inventory total 38,984) |
| F-061..F-075 | `tests/stdlib/fuzz/F100_CONTAINERS_NOTES.md` | seeds `0xF0615EED`..`0xF0755EED` | `F100_OPS >= 2200` (table 2200..2600; tombstone modes execute more per the notes) |
| F-076..F-085 | `tests/stdlib/fuzz/F100_STRING_FILE_NOTES.md` | seeds `0xF076C0DE`..`0xF085C0DE` | 1200 generated valid operations per config |
| F-086..F-093 | `tests/stdlib/fuzz/F100_THREAD_JSON_NOTES.md` | seeds `0xC0FFEE86`..`0xC0FFEE93` | ~1024..1280 generated operations plus bounded lifecycle cycles (per-config table) |
| F-094..F-100 | `tests/stdlib/fuzz/F100_SYSTEM_NOTES.md` | seeds `0xF0941234`..`0xF1001234` | bounded 300..600 cases, plus 8/12-round families (per-config inventory) |

### Duration

No duration was recorded. There is no elapsed-time field in either report and
no per-test or total wall-clock figure to quote. Only limits are visible: 90 s
build and 30 s run in the runner, and `timeout 30` / `timeout -k 5 30` in the
regression and fuzz notes. Duration and performance were not measured here.

### Explicitly not measured

- Sanitizers other than UBSan (address, leak, thread, memory, CFI): absent from
  `modes`; the Phase 1 host probe rejects most of them.
- Engine-driven fuzzing: the three libFuzzer harnesses were SKIPPED; the f100
  corpus is fixed-seed deterministic, not coverage-guided.
- Timing, performance, coverage and memory-footprint measurements. `coverage.py`
  is static call-site reachability; `coverage_exec.sh` is informational only.
- Live-game / Windows x86 behavior. The report was produced by the local
  `build/c+` toolchain on this host.
- "0 failed / 0 timed-out" is limited to this report's classifications. The
  regression notes record development-time test-logic failures that were fixed
  before the final runs; a zero in the final report does not mean nothing ever
  failed. Conversely, a configuration-level PASS can still contain an internal
  `SKIP` line (for example F-040's custom-allocator skip, documented in
  `F100_ALLOC_LIFE_NOTES.md`) because the runner classifies on process exit and
  fixtures, not on sub-case SKIP markers.

### Staleness caveat and exact refresh command

The report's `head` (`ed2ac0f`) matches the current HEAD
(`ed2ac0f1572fa76d7fa03cb250df53090d6d07d0`), so the counts are consistent with
HEAD. However, the report was generated at `07:03:10`, while two tracked libc
files were modified later at `07:14:31`: `libc/CMakeLists.txt` and
`libc/install-include/cplus_libc.h`. The evidence above therefore describes the
tree as of the generation timestamp, not necessarily the current working tree;
treat it as potentially stale and refresh before relying on it for the changed
tree. From the repository root, the exact command that reproduces the final
three-mode report is:

```sh
sh tests/stdlib/run_adversarial.sh --suite all --mode strict-none,default,ubsan
```

It rewrites `tests/stdlib/reports/adversarial.json`, `adversarial.txt` and the
per-mode logs under `tests/stdlib/reports/logs/`. Omitting `ubsan` produces a
smaller, different report, not the one quoted above.

## Honest limitations

- 100 fuzz configurations and passing tests are **not** 100% safety proof.
- Arbitrary raw pointers, uninstrumented precompiled C libraries, compiler
  ownership gaps, Windows x86 ABI, and 27 missing + 49 unsupported ISO C APIs
  remain outside the verified surface.
- Sanitizer results are kept separate from strict-mode functional results.
- `coverage.py` remains static API-call-site reachability; `coverage_exec.sh`
  remains an informational execution-coverage tool, not a correctness gate.
