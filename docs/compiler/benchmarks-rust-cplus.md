# C+ vs Rust: measured performance and memory-safety evidence

Snapshot captured on this host, not a CI or universal result. All numbers below
come from files in this repository; none are estimates.

## Exact toolchains

```
host        : macOS 27.2.1, Darwin 27.2.0 arm64
C+          : C+ 0.1.0, backend host Clang/LLVM (./build/c+ --version)
rustc       : rustc 1.100.0 (0ef1bc386 2026-11-09), LLVM 23.1.5
```

## Methodology

`benchmarks/rust_vs_cplus/run.py` (runner) and `benchmarks/rust_equivalent/*.rs`
(Rust ports of `benchmarks/*.c`):

- seven paired programs; C+ built with `--release --sanitize=none`,
  `--legacy-c --release --sanitize=none`, and `--debug --sanitize=none`;
  Rust built with `--edition 2021 -O -C debuginfo=0` and
  `--edition 2021 -C opt-level=0 -C debuginfo=0` (rustc has no `-O0`).
- one untimed warm-up, then `BENCH_RUNS=5` timed runs; **median wall time** and
  peak RSS from the same run via `/usr/bin/time -l`.
- every configuration must print byte-identical stdout; any mismatch or nonzero
  exit is a hard failure. Results: **35 measurements, 0 failures**.
- raw samples, commands and outputs: `benchmarks/rust_vs_cplus/results/`.

A second independent runner, `benchmarks/cplus_vs_rust/run_benchmark.py`, uses
four paired programs, `perf_counter` best-of-5 and RSS from `time -l`:
**4 pairs, 16 builds, 16 runs, 0 stdout mismatches, 0 failures**. Its results
are in `benchmarks/cplus_vs_rust/results/`.

## Wall-clock and RSS measurements

From `benchmarks/rust_vs_cplus/results/summary.txt` (median seconds; median RSS
in bytes; stdout identical across all five configurations of each program).

| program | c+ safe release | c+ legacy release | c+ debug | rustc -O | rustc opt-level=0 |
| --- | --- | --- | --- | --- | --- |
| bounds_sum | 0.030 s / 1,687,552 B | 0.040 s / 1,687,552 B | 0.070 s / 1,703,936 B | 0.010 s / 1,851,392 B | 0.400 s / 1,884,160 B |
| integer_compute | 0.110 s / 1,671,168 B | 0.110 s / 1,671,168 B | 0.160 s / — | 0.110 s / — | 0.580 s / — |
| pointer_chase | 0.050 s / 17,645,568 B | 0.060 s / 17,678,336 B | 0.080 s / — | 0.040 s / 17,874,944 B | 0.420 s / — |
| function_call | 0.140 s / — | 0.140 s / — | 1.420 s / — | 0.130 s / — | 0.760 s / — |
| alloc_churn | 1.280 s / 1,785,856 B | 1.250 s / — | 1.320 s / — | 0.190 s / 1,900,544 B | 0.380 s / — |
| raii_loop | 0.010 s / — | 0.010 s / — | 0.050 s / — | 0.010 s / — | 0.250 s / — |
| method_dispatch | ≤0.01 s / — | ≤0.01 s / — | 0.120 s / — | ≤0.01 s / — | 0.350 s / — |

These numbers were re-measured after wave 1 of the adversarial campaign; they
supersede the earlier snapshot. During that campaign a prototype
"never reuse retired addresses" runtime policy regressed `alloc_churn` to
**31.02 s (31.6×)**. It was removed rather than accepted: the policy could not
close the stale-free-after-reuse case without an ABI change, and the 31× cost
was not justified. The current runtime performs one `malloc()` per managed
allocation; `alloc_churn` is now 1.21 s versus a 0.98 s pre-campaign baseline
(1.31× after wave 2), with identical stdout.

Full RSS values for every row are in the machine-readable summary; the table
above is intentionally abbreviated. Ratios from the second runner (best time,
peak RSS): `lcg` 0.99× (C+ vs Rust -O), `call_chain` 1.05×, `bounds_sum`
12.34× for that runner's different 200M-iteration workload, `struct_sum` 1.79×.
Rust peak RSS was about 1.8 MiB and C+ about 1.6–1.7 MiB (C+/Rust ratio
0.89–0.92). RSS is page-quantised to 16 KiB; sub-page differences are noise.

### Honest reading

- **Same or close on compute/call-shaped work:** in the first runner,
  `integer_compute` 0.110 s vs Rust -O 0.110 s (**1.00×**),
  `function_call` 0.140 s vs 0.130 s (**1.08×**), `raii_loop` 0.010 s
  vs 0.010 s (**1.00×**). On `bounds_sum` C+ safe is ~3× slower than Rust -O
  on this run; on `alloc_churn` ~6.7× slower (side-table bookkeeping).
- C+ safe release and legacy release are nearly identical on all programs
  (mostly 1.00×), so the native checks cost little in these loops; the large
  allocation difference is the runtime side table, not general safety checks.
- C+ debug (`--debug`) is materially slower, as expected; do not compare it to
  Rust -O. Rust opt-level=0 is a debug-like baseline, not a fair release peer.
- Benchmarks are local, n=5, on a shared laptop; only differences ≥~1.2× that
  reproduce across sessions should be treated as conclusions. No blended
  geomean is claimed.

## Memory-safety evidence (sanitizers disabled)

`tests/compiler/memory_safety_compare/run_suite.py` compares like-shaped
violations with `c+ check/build --strict --sanitize=none` and
`rustc --edition 2021 -C overflow-checks=on` (no sanitizer flags):
**7 categories, 14 cases, 14 passed, 0 failed**; 9 compile-time rejections and
5 runtime traps; C+ 6 compile rejects + 1 trap, Rust 3 compile rejects + 4 traps.

| category | C+ stage / evidence | Rust stage / evidence |
| --- | --- | --- |
| out-of-bounds index | runtime abort, `C+ error: index 7 is outside the bounds of 'a' (length 4)` | runtime panic 101, `index out of bounds: the len is 4 but the index is 7` |
| use-after-free | compile error, `[CPLUS-LIFETIME] use-after-free` | compile error E0505, borrowed value moved |
| double-free | compile error, `[CPLUS-OWNER] double free of 'p'` | compile error E0382, use of moved value |
| invalid free | compile error, `[CPLUS-OWNER] invalid free of interior pointer` | compile error E0308, type mismatch |
| null dereference | compile error, `[CPLUS-LIFETIME] null pointer dereference` | runtime panic 101 (`Option::unwrap` on None) |
| signed overflow | compile error, `[CPLUS-ARITH] signed integer overflow` | runtime panic 101 (`attempt to add with overflow`) |
| invalid shift | compile error, `[CPLUS-FFI] shift count 32 is not less than width 32` | runtime panic 101 (`attempt to shift left with overflow`) |

A bare nonzero exit never counted: compile rejects require rc 1 plus the named
diagnostic and no binary; runtime traps require a built binary plus an exact
exit code and message.

Supporting C+ evidence: bounds matrix 13/13, native arithmetic matrix 24/24,
UAF evidence 12/12 (10 named single-TU shapes statically rejected), runtime
memory-safety suite 28/28, ABI suite 33/33. Earlier paired compile-time probe
(`docs/RUST_COMPARISON.md`): C+ 22/27 vs Rust 19/27 rejections, 17 agreements —
an overlapping subset, not parity.

## What is NOT proven

- No Rust-level borrow checker: C+ tracks only simple same-TU `T *p = &owner`
  borrows; no aliasing/exclusivity, container/field borrows, or lifetime
  annotations.
- No general temporal safety: indirect calls, `uintptr_t` round-trips, arena
  elements, heap address reuse, threads and cross-TU ownership are unsupported.
- No data-race verification and no general pointer provenance model.
- No UB-free guarantee for arbitrary C or for `--legacy-c`; sanitizers are not
  a substitute for these missing checks.
- A passing microbenchmark or safety probe is evidence only for the listed
  cases on this pinned toolchain.

## Reproduce

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-core
unset CPLUS_CLANG
export CPLUS_BIN=$PWD/build/c+
python3 benchmarks/rust_vs_cplus/run.py
python3 benchmarks/cplus_vs_rust/run_benchmark.py
python3 tests/compiler/memory_safety_compare/run_suite.py
cat benchmarks/rust_vs_cplus/results/summary.txt
```
