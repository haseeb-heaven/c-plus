# Real apps/games and ABI preservation — measured proof

All numbers below are produced by permanent runners in this repository. Commands
are exact; no `|| true`, suppression, or silent skips are used. Re-run them after
any compiler change.

## Host and toolchain snapshot

```
host      macOS 27.2.1 arm64
compiler  build/c+ (C+ 0.1.0); sha256 recorded per suite
clang     /usr/bin/clang 16.0.0
gcc       /opt/homebrew/bin/gcc-14 14.2.0 (optional reference)
nm/ar     /usr/bin/nm, /usr/bin/ar
```

## 1. Pinned real C apps — 120 configurations, zero defects

```sh
CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/real_apps/run_suite.py
```

Upstream: `https://github.com/TheAlgorithms/C` pinned at full commit
`e5dad3fa8def3726ec850ca66a7f51521f8ad393`, GPL-3.0-or-later; every source is
downloaded over HTTPS and its SHA-256 checked before use. No upstream source is
redistributed in this repository.

```
counts: cases=10 configs=120 pass=68 safe_mode_rule=46
        reference_rejected=6 failed=0 mismatch_entries=0
source preservation: 38/38 LEGACY configs byte-identical after #line
symbols: LEGACY exact 38/38; safe-mode application symbols 30/30
strict rejections: 34 (SAFE_MODE_RULE=34 DEFECT=0)
```

Each config is run in LEGACY/DEFAULT/STRICT with `--sanitize=none` under
`-std=c89/c99/c11/c17`, and compared byte-for-byte (stdout, exit status, emitted
C, exported symbols) with clang -O2 and gcc-14 -O2. `SAFE_MODE_RULE` =
documented fail-closed rejection; `REFERENCE_REJECTED` = no reference compiler
builds that standard for that source. Neither is counted as a pass; both are
recorded with exact diagnostics. `failed=0` and `mismatch_entries=0` are the
proof that no undocumented defect or behavioral mismatch remains.

Evidence: `tests/compiler/real_apps/summary.txt`, `summary.json`.

## 2. Full playable C games/apps — 6/6, zero failures

```sh
CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/c_apps/run_full_apps.py
```

```
snake_full, game2048_full, tic_tac_toe_full, tetris_mini,
line_editor, calc_repl: legacy=True default=True strict=True
failures: 0
```

Each program is compiled by c+ in all three modes and by clang, run with the
same stdin, and compared for exact stdout and exit status. This suite caught two
real compiler regressions that are now fixed: VLA guard insertion into
multi-dimensional file-scope declarators, and name-keyed extent facts leaking
between functions.

## 3. Unsafe-variant controls — 12/12, zero failures

```sh
CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/c_apps/run_suite.py
```

Five original apps (tic-tac-toe, 2048, snake, calculator, hash tool) plus seven
unsafe variants (OOB write, UAF, null deref, invalid shift, uninitialized read,
double free, overflow) — `c_apps: 12 cases, 0 failures`. This suite caught the
ignored `prev >= 0` guard false positive, now fixed.

## 4. ABI preservation — one gate, five suites

```sh
CPLUS_BIN=$PWD/build/c+ sh tests/compiler/abi_gate/run.sh
```

```
abi_safety         33 passed / 0 failed / 0 skipped
abi_mixed          12 steps, 0 failures
abi_real_world     36 passed / 0 failed / 0 skipped
cross_compiler     26 scenario checks, 6 benchmarks, 0 failures
source_preservation 4 cases, 0 failures
ABI preservation gate: PASS (5 suites)
```

The invariant: ordinary C pointers stay ordinary addresses, struct/union layout
and calling conventions match the host C ABI, and exported application symbols
are unchanged. Only additive `cplus_*` runtime exports are permitted. LEGACY
emitted C is byte-identical to the input after one `#line` prefix. The gate is
wired into both the macOS and Linux CI jobs (`.github/workflows/compiler.yml`).

## 5. Cross-module ownership — 42 checks, zero failures

```sh
CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/abi_ownership/run_suite.py
```

14 multi-TU fixtures × 3 modes: 42 passed, 0 failed. Allocator pairing,
calling convention, layout, linkage and ownership-transfer families pass;
three negative fixtures (allocator mismatch exit 105, cross-TU double release
exit 104, stack-address transfer compile rejection) are detected in every mode.
Rules and limits: `docs/compiler/abi_ownership.md`.

## 6. All-mode scenario campaign — 7,560 executions, zero failures

```sh
CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/all_modes_1000/run.py
```

1,080 distinct scenarios (540 safe, 540 intentional violations) × 7 modes =
7,560 executions: 7,560 pass, 0 fail, 0 timeouts, 84/84 builds, 3,780 expected
violation traps. All safe outputs are byte-identical across the seven modes.
The runner records the compiler sha256 it actually used; re-run after every
rebuild so this evidence tracks the final binary.

## Honest scope

- `SAFE_MODE_RULE` and `REFERENCE_REJECTED` outcomes are disclosed above; they
  are not hidden and not counted as passes. The claim is "no undocumented
  defect or mismatch", not "every configuration compiled".
- Measurements are on macOS/arm64 locally and macOS arm64 + Linux x86-64/arm64
  in CI; Windows is explicitly unsupported until its toolchain path is defined.
- These results do not establish Rust-grade soundness for all C. Known strict
  mode gaps remain tracked in `docs/LIMITATIONS.md` and the matrix in
  `docs/compiler/strict_rules_matrix.json`.
