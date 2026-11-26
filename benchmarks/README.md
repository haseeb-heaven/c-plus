# Benchmarks

Two kinds of measurements live here. Both build the same source three ways:
`c+ --release` with runtime checks, `c+ --release --legacy-c` without them, and
plain `clang -O2` on the lowered-equivalent C.

## Short microbenchmarks

`sh benchmarks/run_bench.sh [name-pattern]` builds every `benchmarks/*.c`,
runs it five times, and reports the median wall-clock time. These are useful
for isolating one runtime feature (allocation bookkeeping, bounds checks,
method lowering) but each test only lasts seconds.

## Long multi-phase workload

`python3 benchmarks/run_complex_bench.py` runs `complex_workload.c`, a
multi-minute workload with four phases:

| Phase | Work measured |
|---|---|
| `matrix` | dense matrix arithmetic with explicit bounds-checked indexing |
| `hash` | many short-lived allocations linked into bucket chains |
| `sort` | `qsort` with a comparator over a large deterministic array |
| `arena` | bump allocation with repeated rewind cycles |

Each phase finishes with a deterministic checksum. The runner builds all
three variants, refuses to report timings unless every checksum matches, and
then records per-phase median nanoseconds, per-phase ratios against plain C,
and a total. This checksum gate prevents an optimizer that deletes work from
appearing as a speedup.

Configuration:

- `CPLUS_BENCH_SCALE` (default `100`) multiplies all phase work. The default
  takes several minutes for each C+ build; set `1` for a smoke test.
- `CPLUS_BENCH_RUNS` (default `1`) repeats each binary and reports medians.
- `CPLUS_BENCH_TIMEOUT` (default `1800` seconds) bounds each child process.
- `CPLUS_BIN` / `CPLUS_CLANG` select the compilers.

The detailed result is written to
`benchmarks/out/complex_benchmark.json`; the console table is a summary.

For a quick smoke check:

```sh
CPLUS_BENCH_SCALE=1 CPLUS_BENCH_RUNS=1 python3 benchmarks/run_complex_bench.py
```

## External toolchain comparisons

Trap-C has no published compiler or source, so it cannot be measured. Fil-C
is Linux-only; `sh tests/compiler/cross_compiler/run_filc_docker.sh` runs the
real upstream compiler in a Linux aarch64 container and writes its results to
`tests/compiler/cross_compiler/results/filc_docker_summary.json`.
