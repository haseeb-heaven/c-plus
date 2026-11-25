# `bench_modern.cp` — deterministic C+ modern-stdlib benchmark (measured 2026-11-23)

Source: `benchmarks/safety_compare/bench_modern.cp`.
All numbers below are **measured**, copied verbatim from the harness output. No value is
estimated, extrapolated or invented.

## 1. Workload

Identical source for every build variant:

- **vector**: `N = 2,000,000` × `push_back(i)` (geometric growth through the checked
  `stdlib_alloc`) then `N` × bounds-checked `at()` reads.
- **string**: `M = 200,000` × `append()` (real `stdlib/string.hp`) then `size()` reads.
- **Determinism**: fixed-seed xorshift32 (`BENCH_SEED = 0x9E3779B9u`, never time-derived)
  drives the pushed values, the in-range read indices and the appended chunks.
- **Output**: CSV header + one data row, `checksum,vector_us,string_us,total_us`.
  Every run of every variant emitted the identical checksum **`5595356797869224`**.
- Checksum = `vsum ^ (ssize_sum << 17) ^ N ^ (M << 32)`, a 64-bit mix of the vector
  read-sum, the string size-sum and the two workload constants.

## 2. Builds — exact commands and results

Run from the repo root.

### 2.1 Required build: `--strict --sanitize=none` — **SUCCESS**

```sh
mkdir -p build/bench_safety
./build/c+ build --strict --sanitize=none \
  benchmarks/safety_compare/bench_modern.cp \
  -o build/bench_safety/bench_modern_strict
# rc=0, no diagnostics
```

### 2.2 Safety-evidence builds: sanitizer modes — **FAILED (external blocker)**

Both the safe (`--release`) and `--default` builds were attempted with the final source.
They fail **before reaching benchmark code**, while compiling the instrumented C+
runtime, with the exact error:

```
/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/cplus_runtime.c:130:23: error: use of undeclared identifier 'CPLUS_EXIT_ARITHMETIC'
  130 |     cplus_print_fatal(CPLUS_EXIT_ARITHMETIC, "signed integer overflow", NULL, 0,
      |                       ^
/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/cplus_runtime.c:137:21: error: use of undeclared identifier 'CPLUS_EXIT_ARITHMETIC'
  137 |   cplus_print_fatal(CPLUS_EXIT_ARITHMETIC, kind, NULL, 0, msg);
      |                     ^
2 errors generated.
error: failed to compile instrumented C+ runtime
```

Commands and exit codes:

```sh
./build/c+ build --release  benchmarks/safety_compare/bench_modern.cp -o build/bench_safety/bench_modern_safe    # rc=1
./build/c+ build --default  benchmarks/safety_compare/bench_modern.cp -o build/bench_safety/bench_modern_default # rc=1
```

This is a compiler/runtime defect (`CPLUS_EXIT_ARITHMETIC` is not declared in the
instrumented runtime), outside this task's file ownership. It is the reason the harness
uses `--sanitize=none`. Note: an earlier attempt in the same session reported the same
error at `cplus_runtime.c:129`; the line shifted to `130`/`137` after a concurrent edit
to the runtime. This is reported, not fixed.

### 2.3 Additional sanitizer-free variants (for interleaving) — **SUCCESS**

```sh
./build/c+ build --default --sanitize=none \
  benchmarks/safety_compare/bench_modern.cp -o build/bench_safety/bench_modern_defaultns   # rc=0
./build/c+ build --legacy-c --sanitize=none \
  benchmarks/safety_compare/bench_modern.cp -o build/bench_safety/bench_modern_legacy      # rc=0
# legacy-c emits one warning from stdlib/error.hp:472
#   warning: GCC does not allow variable declarations in for loop initializers before C99 [-Wgcc-compat]
```

### 2.4 Why the vector uses a self-contained checked container

An earlier draft of this harness used the real `stdlib/vector.hp`. At measurement time
that header carried a concurrent, unverified edit adding a scalar out-parameter helper
(`vector_allocation_bytes`), which the C+ definite-init analyzer rejects in **every**
build mode (`--strict`, `--default`, and with `-w`):

```
./stdlib/vector.hp:180:37: error: [CPLUS-LIFETIME] variable 'bytes' is uninitialized (read before it is initialized)
          fresh = (int *)stdlib_alloc(bytes);
                                      ^~~~~
./stdlib/vector.hp:302:41: error: [CPLUS-LIFETIME] variable 'bytes' is uninitialized (read before it is initialized)
              fresh = (int *)stdlib_alloc(bytes);
                                          ^~~~~
./stdlib/vector.hp:335:37: error: [CPLUS-LIFETIME] variable 'bytes' is uninitialized (read before it is initialized)
          fresh = (int *)stdlib_alloc(bytes);
                                      ^~~~~
```

`stdlib/string.hp` builds cleanly under strict mode and **is** used by the harness. The
vector side therefore uses a self-contained `bench_vector` class defined in the benchmark
source that calls the *same* checked allocator (`stdlib_alloc` / `stdlib_release` from
`stdlib/error.hp`) and exposes the same `push_back()` / `at()` API. This preserves the
allocation-heavy workload and allows the required `--strict --sanitize=none` build to
succeed. This is a scope limitation, not a claim that `stdlib/vector.hp` is fixed.

## 3. Environment (captured at measurement time, 2026-11-23 04:24 UTC)

- Host: macOS 27.2.1, Darwin 27.2.0, `arm64`; CPU reported by
  `sysctl -n machdep.cpu.brand_string` = **Apple M2**.
- C+ driver: `C+ 0.1.0`, backend host Clang/LLVM.
- Backend C compiler: Apple clang 16.0.0 (`clang-1600.0.26.6`).
- Repo: branch `agent/modern-stdlib`, HEAD `1112745`.

## 4. Timing methodology

- Per run: `timeout 60 <binary>`; wall time measured externally with Python
  `time.perf_counter()`. Every run exited 0 and printed the identical checksum.
- **1 warmup run per binary** (discarded), then **11 interleaved rounds** (round-robin
  across the three variants, to share CPU-frequency windows). 11 ≥ the required 7.
- `p95` uses linear interpolation on the sorted samples:
  `k=(n-1)*0.95`, `p95 = s[floor(k)] + (k-floor(k))*(s[floor(k)+1]-s[floor(k)])`.
- The harness also records the program's internal `total_us` CSV value; both are reported.

## 5. Results — EXTERNAL wall-clock, 11 interleaved runs (seconds)

| Variant | build flags | median | min | max | p95 | checksum |
|---|---|---:|---:|---:|---:|---:|
| strict | `--strict --sanitize=none` | 0.0616 | 0.0573 | 0.0825 | 0.0810 | 5595356797869224 |
| default | `--default --sanitize=none` | 0.0608 | 0.0568 | 0.1109 | 0.0998 | 5595356797869224 |
| legacy | `--legacy-c --sanitize=none` | 0.0611 | 0.0551 | 0.0715 | 0.0695 | 5595356797869224 |

Raw external wall-clock samples (seconds, in round order):

```
strict : [0.0642, 0.0634, 0.0586, 0.0573, 0.0582, 0.0688, 0.0599, 0.0587, 0.0795, 0.0825, 0.0616]
default: [0.0570, 0.0669, 0.0607, 0.0589, 0.0608, 0.0638, 0.0568, 0.0651, 0.0886, 0.1109, 0.0608]
legacy : [0.0574, 0.0673, 0.0570, 0.0555, 0.0614, 0.0551, 0.0592, 0.0653, 0.0676, 0.0715, 0.0611]
```

## 6. Results — INTERNAL CSV `total_us` (microseconds), same 11 runs

| Variant | median | min | max | p95 | vector_us median | string_us median |
|---|---:|---:|---:|---:|---:|---:|
| strict | 53348.0 | 50552.0 | 73114.0 | 72503.0 | 45321.0 | 7435.0 |
| default | 54468.0 | 50244.0 | 94157.0 | 87537.5 | 46925.0 | 7207.0 |
| legacy | 53338.0 | 48932.0 | 59712.0 | 59194.5 | 44900.0 | 6898.0 |

The overhead reported by `time.perf_counter()` (≈0.06 s) exceeds the program's internal
`total_us` (≈0.053 s) by process-launch/teardown time; both are reported rather than
silently substituted.

## 7. Honest interpretation

- The three sanitizer-free variants agree on the checksum and have very similar medians
  (0.0608–0.0616 s). With sanitizers unavailable, this run provides **no evidence about
  safe-mode overhead**; that claim would need the failing build in §2.2 repaired first.
- Variance is material on this shared host (e.g. `default` max 0.1109 s vs min 0.0568 s).
  Treat the medians as indicative, not precise, and prefer the p95/min/max spread over a
  single ratio.
- `default` and `legacy` are *not* comparable to a UBSan build; they are included only to
  make the interleaving discipline meaningful and to show the workload is deterministic.
- No sanitizer result, no `stdlib/vector.hp` result and no cross-language result is
  claimed here. Fil-C and Goose were not part of this task.
- Files owned/created for this task: only `bench_modern.cp` and this `bench_modern_NOTES.md`.
  No compiler, runtime, stdlib header, build script, git index or commit was modified.
