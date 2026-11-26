# Benchmark evidence (committed binaries + raw output + screenshots)

Every number comes from a binary built from the committed source and run on
this host (Apple clang 16 / arm64, restricted PATH, bounded timeout). Raw
stdout, the binaries, build logs and SVG screenshots are all in this folder.

| Benchmark | Source | Binary | Raw result | Screenshot |
|---|---|---|---|---|
| scoped-memory-style bump vs checked heap | [bump_bench.cp](../../benchmarks/scoped_bump/bump_bench.cp) | [bump_bench](bin/bump_bench) | [scoped_bump.out](results/scoped_bump.out) | [scoped_bump.svg](screenshots/scoped_bump.svg) |
| Modern libs on scratch | [bench.cp](../../benchmarks/scoped_apps/bench.cp) | [scoped_apps](bin/scoped_apps) | [scoped_apps.out](results/scoped_apps.out) | [scoped_apps.svg](screenshots/scoped_apps.svg) |

Measured on the committed run:

```text
BENCH bump_ns_per_op 30
BENCH heap_ns_per_op 328
SCOPED_APPS bump_ns_per_op 23
SCOPED_APPS heap_ns_per_op 206
SCOPED_APPS modern_ns_per_op 146
```

That is ~10.9x and ~9.0x lower allocation latency for the scoped-memory-style bump
path in these two runs. Earlier runs measured 86/261 ns and 60/162 ns; timings
are host/load dependent, so the deterministic gates only assert bucketed
verdicts (`BENCH_SCOPED 4 4 0`, `SCOPED_APPS_BENCH 3 3 0`). No equivalence with
Goose/Rust/Fil-C is claimed.

Cross-language *outcome* evidence (not timing): `tests/rust_vs_cplus/run_compare.sh`
reports 473 matched / 119 documented differences / 0 failures, and
`tests/stdlib/upstream_reuse/run_memory_safety.sh` reports `MEMSAFE 5 5 0`.

`SHA256SUMS.txt` pins the binaries and raw outputs.

## Head-to-head rounds: C+ vs Rust (committed raw data)

Runner: [`benchmarks/language_rounds/run.sh`](../../benchmarks/language_rounds/run.sh);
sources: [`bench.cp`](../../benchmarks/language_rounds/bench.cp) and
[`bench.rs`](../../benchmarks/language_rounds/bench.rs); raw result:
[`language_rounds.txt`](language_rounds.txt); screenshot:
[`language_rounds.svg`](screenshots/language_rounds.svg).

5 rounds of an identical 2M-iteration integer/struct workload:

| Round | C+ ns | Rust ns | Winner |
|---|---:|---:|---|
| 1 | 19419000 | 2260500 | Rust |
| 2 | 28790000 | 2284750 | Rust |
| 3 | 22856000 | 3040792 | Rust |
| 4 | 19778000 | 2265625 | Rust |
| 5 | 21166000 | 2266583 | Rust |

**Rust won 5/5 on this compute loop.** This is reported honestly: the C+
advantage measured above is on **memory allocation** (bump/arena/pool), not on
raw arithmetic loops. No claim that C+ is faster at everything.

`trapc` is **NOT TESTABLE**: no public compiler, source, or license has been
published, so no comparison number exists. Fil-C is now **TESTED** using the
real upstream release: the published compiler is Linux-only, so
`tests/compiler/cross_compiler/run_filc_docker.sh` runs it (and the binaries it
produces) in a Linux aarch64 Docker container with this repository mounted.
Results are in `tests/compiler/cross_compiler/results/filc_docker_summary.json`:
const-oob and null-deref trap (exit 133), signed-overflow and clean scenarios
run cleanly. These are real measurements, not macOS-native numbers.
