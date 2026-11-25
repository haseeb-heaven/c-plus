# Benchmark evidence (committed binaries + raw output + screenshots)

Every number comes from a binary built from the committed source and run on
this host (Apple clang 16 / arm64, restricted PATH, bounded timeout). Raw
stdout, the binaries, build logs and SVG screenshots are all in this folder.

| Benchmark | Source | Binary | Raw result | Screenshot |
|---|---|---|---|---|
| Goose-style bump vs checked heap | [bump_bench.cp](../../benchmarks/goose_fast/bump_bench.cp) | [bump_bench](bin/bump_bench) | [goose_fast.out](results/goose_fast.out) | [goose_fast.svg](screenshots/goose_fast.svg) |
| Modern libs on scratch | [bench.cp](../../benchmarks/goose_modern/bench.cp) | [goose_modern](bin/goose_modern) | [goose_modern.out](results/goose_modern.out) | [goose_modern.svg](screenshots/goose_modern.svg) |

Measured on the committed run:

```text
BENCH bump_ns_per_op 30
BENCH heap_ns_per_op 328
GOOSE_MODERN bump_ns_per_op 23
GOOSE_MODERN heap_ns_per_op 206
GOOSE_MODERN modern_ns_per_op 146
```

That is ~10.9x and ~9.0x lower allocation latency for the Goose-style bump
path in these two runs. Earlier runs measured 86/261 ns and 60/162 ns; timings
are host/load dependent, so the deterministic gates only assert bucketed
verdicts (`GOOSE_BENCH 4 4 0`, `GOOSE_MODERN_BENCH 3 3 0`). No equivalence with
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

`trapc` and `fil-c`/`filc` binaries are **NOT INSTALLED** on this host, so those
comparisons are explicitly `NOT_TESTED` here — no fabricated numbers. Only the
Fil-C-style *technique* (side-table liveness queries) is adapted in C+.
