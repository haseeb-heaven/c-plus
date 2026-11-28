# Upstream-shaped compiler safety regression suite

Native (compiler-binary) suite for C+ safety diagnostics and runtime traps. It
owns only new files under `tests/compiler/upstream_ports/`; it does not build
the compiler, touch other test trees, or make commits.

## Run

```sh
cd <repo-root>
CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/upstream_ports/run_suite.py
```

Optional knobs: `UP_TIMEOUT` (per-subprocess hard timeout, default 90s) and
`UP_WORKERS` (parallel workers, default 4). The runner exits 0 only when every
declared expectation matches.

## What "upstream port" means here

Every case is **independently authored C** that mirrors the *shape* (not the
source text) of an upstream test. Upstream origin — repository, path, pinned
SHA and licence — is recorded per case in `manifest.json`, together with a
`shape` note explaining the mirrored pattern. The four origin families are:

| Family | Upstream | Pinned SHA | Licence | Cases |
| --- | --- | --- | --- | --- |
| `checkedc_dynamic_bounds` | `checkedc/checkedc-clang`, `clang/test/CheckedC/dynamic-checks/` | `a2843eae17d96cbfad2cf386dd60de0b849cfb6d` | Apache-2.0 WITH LLVM-exception | 5 |
| `apple_bounds_traps` | `swiftlang/llvm-project`, `clang/test/BoundsSafety{,-legacy-checks}/` | `ca74e48841e588074fba77543e4e0fc643edc52c` | Apache-2.0 WITH LLVM-exception | 5 |
| `clang_sa_malloc_divzero` | `llvm/llvm-project`, `clang/test/Analysis/` | `14fee62eb543435c407837d2c07e8054be3de90d` | Apache-2.0 WITH LLVM-exception | 5 |
| `softboundcets_spatial_temporal` | `santoshn/softboundcets-34`, `tests/test.c` | `9a9c09f04e16f2d1ef3a906fd138a7b89df44996` | BSD-3-Clause | 5 |

The Checked C and LLVM SHAs were taken from local clones under
`c-plus-upstreams/`; the SoftBoundCETS SHA was taken from the GitHub API.

## Exact assertions

For each case and each mode (`--default`, `--strict`) with `--sanitize=none`:

* `check` — exact return code, exact normalised diagnostic substrings (full
  `<SRC>:line:column: error|warning: [CPLUS-*] ...` header lines and `help:` /
  `note:` lines), and forbidden markers (e.g. no `check: <SRC> ok` on rejection).
* `build` — same assertions for the build step, plus binary presence/absence.
* `run` — exact return code (including signal codes such as `-6` for the
  `abort` trap), exact stdout and exact stderr. Trap messages are compared
  byte-for-byte after path normalisation; a bare nonzero exit is never proof.

Compiler crashes (negative return code from `check`/`build`) and subprocess
timeouts are hard failures. Source hashes in the manifest are verified on every
run, so silently drifting a fixture is a failure rather than a false pass.

Mode differences are asserted explicitly: for example `sa_divzero_constant`
must be a DEFAULT warning with a successful build/run but a STRICT error with
no binary, while `sa_divzero_dynamic` must be a DEFAULT warning followed by the
exact runtime division-by-zero trap (exit 110) and a STRICT compile error.

## Results

Written to `results/`:

* `summary.txt` — counts (cases, mode checks, per family, per kind) and all
  exact failures.
* `summary.json` — machine-readable record with commands, return codes,
  normalised outputs and provenance for every case/mode/step.
* `logs/<case>.<mode>.<step>.txt` — full normalised output for each subprocess.
* `bin/` — binaries produced by accepted builds (regenerated each run).

## Last verified run

`20 cases / 20 passed, 0 failed` (40 mode checks, 0 failure entries) against
`build/c+` (C+ 0.1.0) on 2026-11-23 using `--default --sanitize=none` and
`--strict --sanitize=none`. Kinds: 9 static rejects, 4 runtime aborts, 2 runtime
traps, 2 warn-then-trap cases, 2 warn-then-STRICT-reject cases, 1 clean control.
