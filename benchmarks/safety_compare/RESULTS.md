# Speed comparison: C+, Rust, Goose, plain C (measured 2026-11-22)

Command: `CPLUS_GOOSE=<path> sh benchmarks/safety_compare/run_bench.sh`
Host: macOS arm64, Apple clang 16, rustc 1.100.0, Goose v2026.4, C+ driver at
`-O2 --release`. Each binary ran 5 times; the first run was discarded as
warmup and the median of the remaining 4 is reported. All four languages
produced identical checksums for both workloads.

| Workload | plain C `-O2` | C+ safe (`--release`, UBSan) | C+ legacy (`--release --legacy-c`) | Rust `-O` | Goose |
|---|---:|---:|---:|---:|---:|
| loop sum (2e8) | 0.0036s | 0.1075s | 0.0038s | 0.0042s | 0.0043s |
| nested (4000²) | 0.0023s | 0.0090s | 0.0023s | 0.0026s | 0.0024s |

Ratios vs plain C: safe 29.85x / 3.90x; legacy 1.05x / 1.02x; Rust 1.18x /
1.14x; Goose 1.19x / 1.06x.

## Honest interpretation

- **The fairest comparison is C+ safe vs C+ legacy**: same driver, same
  `-O2 --release`, only the safe-mode UBSan instrumentation differs. That
  overhead is ~30x on the integer loop and ~3.9x on the nested loop. Those are
  real numbers on this host, not claims about all workloads.
- **All non-instrumented compilers folded the arithmetic loops into closed-form
  results** (identical checksums prove semantics, but the 0.002–0.004s timings
  are dominated by that optimisation, not by loop execution). These numbers
  must NOT be read as "Rust/Goose are 30x faster than C+"; they are
  optimisation differences.
- Fil-C was **not measured**: no `filc` toolchain is installed. No result is
  fabricated for it.
- `goose` is a real installed compiler here, but it is outside the restricted
  benchmark PATH; pass its path via `CPLUS_GOOSE`.
- This benchmark measures two synthetic integer workloads only. It is not a
  general performance claim about any language.
