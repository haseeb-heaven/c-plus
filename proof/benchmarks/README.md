# Benchmark evidence (committed binaries + raw output + screenshots)

Every number comes from a binary built from the committed source and run on
this host (Apple clang 16 / arm64, restricted PATH, bounded timeout). Raw
stdout, the binaries, build logs and SVG screenshots are all in this folder.

| Benchmark | Source | Binary | Raw result | Screenshot |
|---|---|---|---|---|
| scoped-memory-style bump vs checked heap | [bump_bench.cp](../../benchmarks/scoped_bump/bump_bench.cp) | [bump_bench](bin/bump_bench) | [scoped_bump.out](../all_apps_games_screenshots/README.md) | [scoped_bump.svg](../all_apps_games_screenshots/README.md) |
| Modern libs on scratch | [bench.cp](../../benchmarks/scoped_apps/bench.cp) | [scoped_apps](../all_apps_games_screenshots/README.md) | [scoped_apps.out](../all_apps_games_screenshots/README.md) | [scoped_apps.svg](../all_apps_games_screenshots/README.md) |

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
the inspiration language/Rust/Fil-C is claimed.

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

## Cross-language matrix (measured on this host)

All rows use the same 2,000,000-iteration integer/struct workload and checksum
`16776346274400`. Sources: `language_rounds/{bench.cp,bench.rs,bench.zig,bench.goose}`;
runners: `language_rounds/{run.sh,run_zig.sh,run_goose.sh}`; raw data:
`language_rounds.txt`, `zig_rounds.txt`, `goose_rounds.txt`.

| Language | Build | 5 rounds (ms) | Median (ms) | Evidence |
|---|---|---:|---:|---|
| Rust | `rustc -O` | 2.244, 2.384, 2.384, 2.458, 2.517 | 2.384 | `language_rounds.txt`, Rust won 5/5 |
| Zig | `zig build-exe -O ReleaseFast` | 2.431, 2.492, 2.492, 2.505, 2.555 | 2.492 | `bench.zig`, `run_zig.sh` |
| C+ release | `c+ --release --sanitize=none` | 2.613, 2.613, 3.274, 3.569, 4.566 | 3.274 | local release probe, same source |
| Goose-lang | `goose run` | 3.395, 3.503, 3.609, 4.383, 5.313 | 3.609 | `goose_rounds.txt` |
| C+ default | `c+ --default --sanitize=none` | 20.080, 20.254, 20.404, 21.934, 22.766 | 20.804 | `language_rounds.txt` |
| Fil-C | toolchain absent | — | — | **NOT_TESTED**: no `fil-c`/`filc` binary; only a liveness-query technique is adapted in C+ |
| TrapC | compiler unreleased | — | — | **NOT_TESTED**: no published compiler/source; design-stage per `docs/security/RUNTIME_SAFETY_TRAPC.md` |

Do not read this as a general speed ranking: C+ default has sanitizer/lowering
overhead, the Goose runner includes its own compile path, and the two absent
toolchains cannot be measured here. Rust still won the committed 5/5 head-to-head.

## Heavy benchmark (200,000,000 iterations x 150 rounds)

Raw per-round timings are in `heavy/{C,CPLUS,RUST,ZIG,GOOSE}.txt`; every round
reported checksum `1677765674676096`. ns/iter = elapsed_ns / 2e8.

| Language | Median ns/iter | Min | Max |
|---|---:|---:|---:|
| C (`clang -O2`) | 1.948 | 1.514 | 3.358 |
| Goose-lang | 2.061 | 1.592 | 16.162 |
| C+ release (`--sanitize=none`) | 2.743 | 1.830 | 7.240 |
| Zig (`ReleaseFast`) | 3.121 | 1.345 | 13.624 |
| Rust (`rustc -O`) | 3.744 | 1.827 | 16.977 |

This heavy loop reverses the light 5/5 result (C+ release median faster than
Rust here); different workload scale, optimization policy and single-host noise.
Fil-C/TrapC remain NOT_TESTED.

## Allocation / scoped memory (C+ only)

| Benchmark | Measured |
|---|---|
| Bump vs checked heap | `30 ns/op` vs `328 ns/op` (~10.9x) |
| Scoped apps | bump `23 ns/op`, heap `206 ns/op`, modern libs `146 ns/op` |
| Modern allocators | heap `5,637,000 ns`, arena `916,000 ns` (6.15x), scratch `1,329,000 ns` (4.24x) |

`trapc` and `fil-c`/`filc` binaries are **NOT INSTALLED** on this host, so those
comparisons are explicitly `NOT_TESTED` here — no fabricated numbers. Only the
Fil-C-style *technique* (side-table liveness queries) is adapted in C+.
