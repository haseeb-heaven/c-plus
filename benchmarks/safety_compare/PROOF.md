# Proof record — C+ safety and speed evidence (branch `agent/modern-stdlib`)

**Date:** 2026-11-23. **Repository:** `/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib`.

This document is a **synthesis of already-recorded evidence**, not a new experiment.
Every number below is copied from an existing sibling-agent NOTES file or an
actual result artifact, which is named at each point of use. Nothing is
estimated, extrapolated, or fabricated. Where a sibling artifact is missing the
item is marked **PENDING**. The headline claims and their limits are stated in
§8 ("What this proves / does not prove"); read that section before using any
number here.

---

## 1. Environment and tool versions (as recorded by the sibling artifacts)

| Item | Value | Source |
|---|---|---|
| Branch / HEAD | `agent/modern-stdlib`; evidence collected at HEAD `cbcea57174e5c6b26d07bcc4e1b5c8f7501b340a` (merge, 2026-11-23T09:16:38+05:30) | `docs/security/verification-status.md` §1 |
| Host | macOS arm64 / Darwin `27.2.0`; Apple silicon (`RELEASE_ARM64_T8112`) | `RESULTS.md`, `MEMORY_USAGE.md` §Environment |
| CPU / machine | Authoritative `sysctl` record in `METHODOLOGY.md` §6: Apple **M2** (`hw.model = Mac14,7`, `arm64`, T8112), 8 logical cores, 8 GiB RAM. `RESULTS.md`'s earlier "Apple M3 Pro" label is superseded by this record. | `METHODOLOGY.md` §6 |
| C+ driver | `build/c+`, `C+ 0.1.0`, backend host Clang/LLVM; `-O2 --release` for Safe, `--release --legacy-c` for LegacyC | `RESULTS.md`, `verification-status.md` §1 |
| C/C++ backend | `/usr/bin/clang` = Apple clang **16.0.0** (`clang-1600.0.26.6`), target `arm64-apple-darwin27.2.0` | `RESULTS.md`, `MEMORY_USAGE.md`, verified live with `/usr/bin/clang --version` |
| Default `clang` on PATH | Homebrew clang **23.1.6** (targets `arm64-apple-macosx27.0.0`, no `MacOSX27.sdk` installed) — used for baseline ASan/TSan evidence in the CVE notes; also the cause of two prerequisite-build aborts (§5) | `verification-status.md` §1/§3.2 |
| Rust | rustc **1.100.0** (`0ef1bc386` 2026-11-09, LLVM 23.1.5), `-O`; verified live with `rustc --version` | `bench_rust_NOTES.md`, `RESULTS.md` |
| Goose | **v2026.4-57-g9412b80f** (runtime `41e336e5d1ee`), default `-O2`; verified live with `goose --version` | `RESULTS.md`, `MEMORY_USAGE.md` |
| Sanitizer backend capability | Accepted by probe: `none` (`strict-none`), `undefined` (UBSan), `integer`. **Unsupported** by `/usr/bin/clang`: ASan/`address`, `leak`, `thread`, `memory`, `cfi` | `verification-status.md` §4, `tests/stdlib/agent2/SANITIZER_MATRIX.md` |
| Fil-C | **not measured** — no `filc` toolchain installed. No result fabricated | `RESULTS.md`, `MEMORY_USAGE.md` |

All builds in the speed work succeeded with **no warnings** (`RESULTS.md`,
`bench_rust_NOTES.md`). The C+ safe build applies UBSan instrumentation
(`-fsanitize=undefined,bounds,null,alignment`, `-fno-sanitize-recover=all`,
`-fno-omit-frame-pointer`); the legacy build uses `-std=gnu89`,
`-DCPLUS_LEGACY_C=1` and no sanitizers. This instrumentation difference is the
**only** intended difference between the two C+ variants.

---

## 2. Workloads and checksums (semantics check)

All variants of a given workload printed the identical checksum, which is the
evidence that the compared processes did the same integer work.

| Workload | Work done | Checksum (identical for all compared variants) | Source |
|---|---|---|---|
| `loop` | 2×10⁸ integer sum | `19999999900000000` | `MEMORY_USAGE.md` §Workloads; `RESULTS.md` |
| `nested` | 4000² nested sum | `16000000` | `MEMORY_USAGE.md` §Workloads; `RESULTS.md` |
| `modern` | 2×10⁶ `vector::push_back` + `at()` reads; 2×10⁵ `string::append` + `size()` reads | `2200000200000` | `RESULTS.md` §Modern workload; `bench_rust_NOTES.md` §Workload equivalence; `MEMORY_USAGE.md` |
| allocator: `alloc`/`free`/`realloc`/`small`/`large`/`grow` | 200,000 ops (4,000 for `large`), xorshift32 seed `314159265`, 2 warmup + 11 measured rounds | no cross-language checksum (C-only benchmark); raw CSV is the artifact | `benchmarks/stdlib/allocator/NOTES.md`, `results/allocator_bench.csv` |

`modern.cp` prints `vsum + ssize_sum`; the closed form is
`vsum = N*(N-1)/2 = 1_999_999_000_000`, `ssize_sum = Σ_{k=1..M}(1+10k) =
200_001_200_000`, total **`2200000200000`**. `bench_modern.rs` uses
`black_box` only to keep the sum observable. The checksum being equal verifies
the integer result for **one deterministic input**; it is not a correctness or
safety proof (§8).

---

## 3. Speed — exact commands, medians and p95

### 3.1 `loop` and `nested` (5 runs, first discarded as warmup, median of 4)

Exact command (from repo root): `CPLUS_GOOSE=<path> sh benchmarks/safety_compare/run_bench.sh`
(`run_bench.sh` pins `PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin`
and `/usr/bin/clang` for the two C+ variants). Goose was passed via
`CPLUS_GOOSE=/Users/haseeb-mir/.local/bin/goose` because it sits outside the
restricted PATH. All four languages produced the identical checksum for both
workloads. Source: `RESULTS.md` lines 1–33.

| Workload | plain C `-O2` | C+ safe (`--release`, UBSan) | C+ legacy (`--release --legacy-c`) | Rust `-O` | Goose |
|---|---:|---:|---:|---:|---:|
| loop sum (2e8) | 0.0036 s | 0.1075 s | 0.0038 s | 0.0042 s | 0.0043 s |
| nested (4000²) | 0.0023 s | 0.0090 s | 0.0023 s | 0.0026 s | 0.0024 s |

Ratios vs plain C: safe **29.85× / 3.90×**; legacy 1.05× / 1.02×; Rust 1.18× /
1.14×; Goose 1.19× / 1.06×. `run_bench.sh`/`RESULTS.md` report medians only;
**p95 for these 5-variant rows: PENDING** (the n=31 p95 values in §3.5 are for
different build configurations — sanitizers-off C+ vs legacy C).

### 3.2 `modern` — same source, safe vs legacy (7 interleaved runs, 1 warmup)

Exact standalone commands (from repo root; `RESULTS.md` §Exact commands):

```sh
mkdir -p build/bench_safety
PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin \
  build/c+ build --release \
  benchmarks/safety_compare/modern.cp -o build/bench_safety/cplus_safe_modern
PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin \
  build/c+ build --release --legacy-c \
  benchmarks/safety_compare/modern.cp -o build/bench_safety/cplus_legacy_modern
rustc -O benchmarks/safety_compare/modern.rs -o build/bench_safety/rust_modern
"$CPLUS_GOOSE" build benchmarks/safety_compare/modern.goose \
  -o build/bench_safety/scoped_apps
```

Timing used 1 warmup then 7 round-robin interleaved runs per binary with
`time.perf_counter()`; median of the 7 reported (the Python driver is quoted in
`RESULTS.md` lines 88–102).

| Variant | median | min | max | checksum |
|---|---:|---:|---:|---:|
| C+ safe (`--release`, UBSan) | 0.0140 s | 0.0134 s | 0.0188 s | 2200000200000 |
| C+ legacy (`--release --legacy-c`) | 0.0061 s | 0.0052 s | 0.0077 s | 2200000200000 |
| Rust (`-O`) | 0.0060 s | 0.0048 s | 0.0105 s | 2200000200000 |
| Goose (`-O2`) | 0.0044 s | 0.0041 s | 0.0090 s | 2200000200000 |

Ratio of medians vs C+ legacy: safe **2.31×**, Rust 0.99×, Goose 0.72×. Across
three non-interleaved passes the safe/legacy median ratio ranged ~1.8×–3.1×;
the interleaved median is the most controlled estimate and the ratios are
indicative, not precise (`RESULTS.md` §Honest interpretation). **p95 for these
four variants: PENDING** (the recorded table has min/max only; the Rust
`bench_modern` p95 is in §3.3, and `METHODOLOGY.md` §3 explicitly declines to
report p95 for n<20 interleaved samples).

### 3.3 Rust `bench_modern` — 31 timed runs, with p95 (sibling note)

Source: `benchmarks/safety_compare/bench_rust_NOTES.md`. The binary was built
with `rustc -O benchmarks/safety_compare/bench_modern.rs -o <out>/bench_modern`
(rustc 1.100.0) into a temp directory outside the repo, and each of 1 warmup +
31 timed runs invoked as `/usr/bin/timeout 60 <out>/bench_modern` with a Python
60 s guard. p95 uses linear interpolation at rank `0.95*(n-1) = 28.50`.

| Metric | Value |
|---|---:|
| median | **0.006519 s** |
| min | 0.006080 s |
| max | 0.007617 s |
| **p95** | **0.007210 s** |
| mean | 0.006557 s |
| checksum (all 32 runs) | `2200000200000` |

Timing includes small `timeout` fork/exec overhead. Two earlier 15-run passes
gave medians 0.009347 s and 0.006595 s — shared-host drift, not a language
property. The note explicitly states this benchmark proves no safety property.

### 3.4 Allocator microbenchmark — medians **and p95** (raw CSV)

Source: `benchmarks/stdlib/allocator/NOTES.md` and unedited
`benchmarks/stdlib/allocator/results/allocator_bench.csv`. Exact command:

```sh
sh benchmarks/stdlib/allocator/run.sh
# runner executes:
/usr/bin/clang -O2 -std=gnu17 \
  -I/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/libc/install-include \
  benchmarks/stdlib/allocator/allocator_bench.c \
  build/libc/libcplus_hardened.a -lm -pthread \
  -o benchmarks/stdlib/allocator/allocator_bench
benchmarks/stdlib/allocator/allocator_bench --runs 11 --warmup 2 --seed 314159265
```

The program itself computes min/median/max/nearest-rank p95/ns-per-op; no
post-processing. Variants: `raw` (host malloc, untracked control), `tracked`
(hardened C+ allocator, metadata side table), `query` (tracked + debug queries
after each op). Host: Darwin 27.2.0 arm64, Apple M2, Apple clang 16.0.0;
~27 s wall clock.

| Workload | Variant | ops | min (s) | **median (s)** | max (s) | **p95 (s)** | ns/op |
|---|---|---:|---:|---:|---:|---:|---:|
| alloc | raw | 200000 | 0.001879 | **0.004857** | 0.011732 | 0.010294 | 24.29 |
| alloc | tracked | 200000 | 0.020191 | **0.044537** | 0.273962 | 0.085672 | 222.69 |
| alloc | query | 200000 | 0.027686 | **0.045455** | 0.092937 | 0.067222 | 227.27 |
| free | raw | 200000 | 0.001410 | **0.001783** | 0.003374 | 0.003339 | 8.92 |
| free | tracked | 200000 | 0.193762 | **0.276622** | 0.377117 | 0.325476 | 1383.11 |
| free | query | 200000 | 0.222505 | **0.250755** | 0.370457 | 0.315792 | 1253.77 |
| realloc | raw | 200000 | 0.013178 | **0.016636** | 0.026235 | 0.024180 | 83.18 |
| realloc | tracked | 200000 | 0.015124 | **0.019575** | 0.042534 | 0.039430 | 97.87 |
| realloc | query | 200000 | 0.021354 | **0.024689** | 0.062607 | 0.041361 | 123.44 |
| small | raw | 200000 | 0.006451 | **0.007001** | 0.008948 | 0.008485 | 35.00 |
| small | tracked | 200000 | 0.014872 | **0.016342** | 0.020491 | 0.019439 | 81.71 |
| small | query | 200000 | 0.018262 | **0.019500** | 0.026854 | 0.023702 | 97.50 |
| large | raw | 4000 | 0.000487 | **0.000675** | 0.000722 | 0.000697 | 168.75 |
| large | tracked | 4000 | 0.000603 | **0.000829** | 0.000901 | 0.000857 | 207.25 |
| large | query | 4000 | 0.000591 | **0.000957** | 0.000978 | 0.000963 | 239.25 |
| grow | raw | 200000 | 0.108095 | **0.125590** | 0.167543 | 0.164367 | 627.95 |
| grow | tracked | 200000 | 0.112554 | **0.141173** | 0.185093 | 0.181013 | 705.86 |
| grow | query | 200000 | 0.116176 | **0.141505** | 0.180323 | 0.180085 | 707.53 |

Derived median ratios: tracked/raw = alloc 9.17×, free 155.1×, realloc 1.18×,
small 2.33×, large 1.23×, grow 1.12× (full table in NOTES §Derived comparisons).
The free-path cliff is a recorded **finding**: `recent_add()` linearly probes a
saturated `g_recent[1024]` cache at >1024 distinct freed addresses. A
`CPLUS_MALLOC_TRACKING` toggle isolates tracking ON vs OFF from one source:
median 0.004004 s (OFF, 20.02 ns/op) vs 0.012287 s (ON, 61.44 ns/op) —
~3.07×, with a second pass at ~2.93× (NOTES §Is the debug-metadata overhead
isolated?).

### 3.5 Sanitizers-off safe C+ vs legacy C — medians **and p95** (n=31)

Source: `benchmarks/safety_compare/legacy_speed_NOTES.md` (sibling artifact
appeared during synthesis). This isolates C+'s **language-level safe lowering**
from UBSan instrumentation: both `loop.c` and `nested.c` are compiled with
`build/c+ build --strict --sanitize=none --release` (no sanitizer) against a
plain-C twin `legacy_speed.c` built with `/usr/bin/clang -O2`. Exact commands:

```sh
TD=<external temp dir>   # e.g. /private/var/folders/.../opencode/igi_speed
/usr/bin/clang -O2 benchmarks/safety_compare/loop.c -o "$TD/c_loop"
/usr/bin/clang -O2 benchmarks/safety_compare/nested.c -o "$TD/c_nested"
/usr/bin/clang -O2 benchmarks/safety_compare/legacy_speed.c -o "$TD/c_twin"
PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin \
  build/c+ build --strict --sanitize=none --release \
    benchmarks/safety_compare/loop.c -o "$TD/cp_loop"
PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin \
  build/c+ build --strict --sanitize=none --release \
    benchmarks/safety_compare/nested.c -o "$TD/cp_nested"
```

`--release` is required to avoid conflating `-O0` with safety mode. `--emit-c`
plus `diff` showed that for these pure-arithmetic loops safe mode emits C
byte-identical to the source apart from a `#line` marker — i.e. **no hot-loop
checks are inserted**. 1 warmup + 31 interleaved timed runs (pass 2, primary);
p95 by linear interpolation (type 7). Checksums were identical across all
three variants for each workload.

| Workload | Variant | median | min | max | p95 | checksum |
|---|---|---:|---:|---:|---:|---:|
| loop | legacy `clang -O2 loop.c` | 0.004943 s | 0.002117 s | 0.007673 s | 0.007024 s | 19999999900000000 |
| loop | safe C+ `--strict --sanitize=none --release` | 0.005044 s | 0.002187 s | 0.016466 s | 0.007198 s | 19999999900000000 |
| loop | legacy twin `legacy_speed.c` | 0.005096 s | 0.002149 s | 0.010464 s | 0.005977 s | 19999999900000000 |
| nested | legacy `clang -O2 nested.c` | 0.004811 s | 0.002175 s | 0.005560 s | 0.005301 s | 16000000 |
| nested | safe C+ `--strict --sanitize=none --release` | 0.004996 s | 0.002195 s | 0.007272 s | 0.005800 s | 16000000 |
| nested | legacy twin `legacy_speed.c` | 0.004841 s | 0.002135 s | 0.007009 s | 0.005781 s | 16000000 |

Median ratios vs legacy `clang -O2`: loop safe C+ **1.020×**, nested safe C+
**1.039×** (pass 1, n=11: 0.928× and 1.137×). Interpretation from the note:
**no measurable safe-mode overhead** with sanitizers off — consistent with
`RESULTS.md`'s warning that its ~30×/3.9× figures are instrumentation effects.
These timings are dominated by process spawn/teardown and `printf` because
`-O2` folds the loops into a closed form; ratios near 1.0× mean "no detectable
overhead", not faster code generation. Host noise moved the legacy median from
~3.0 ms (pass 1) to ~4.9 ms (pass 2).

### 3.6 Allocator cross-check: C+ `stdlib_alloc` vs Rust `Vec<u8>` (n=11)

Source: `benchmarks/safety_compare/ALLOC_COMPARE_NOTES.md` (sibling artifact).
C+ uses the supported `stdlib_alloc`/`stdlib_release` path from
`stdlib/error.hp` (which calls the runtime's `cplus_alloc`/`cplus_release` in
`libcplus_runtime.a` — no extra archive, no link error). Rust uses
`vec![0u8; sz]`. `N = 200,000` alloc+free pairs, sizes 8…64 B; value-based
checksum (no pointer values) = **`4750706934088118880`** on all 1 warmup + 11
interleaved measured runs in both binaries (also reproduced independently in
Python). Exact build commands:

```sh
mkdir -p build/bench_safety
PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin \
  build/c+ build --strict --sanitize=none \
  benchmarks/safety_compare/alloc_bench.cp -o build/bench_safety/cplus_alloc_bench
rustc -O benchmarks/safety_compare/alloc_bench.rs -o build/bench_safety/rust_alloc_bench
```

Build caveat recorded in the note: the C+ driver invoked `--verbose` showed
`-std=gnu17 -O0 -g -pthread -DCPLUS_SAFE=1` with no sanitizers (no `--release`
was passed), so **C+ is at -O0 and Rust at -O** — this is *not* an
apples-to-apples allocator comparison. A C+ `--release --strict --sanitize=none`
(-O2) variant was **not measured**; no number is claimed for it.

| Variant | checksum | ops | min (s) | median (s) | max (s) | p95 (s) | median ns/op |
|---|---|---:|---:|---:|---:|---:|---:|
| C+ `stdlib_alloc`/`release` (`--strict --sanitize=none`, -O0) | 4750706934088118880 | 200000 | 0.033627 | **0.039260** | 0.049478 | 0.047966 | 196.30 |
| Rust `Vec<u8>` (`-O`) | 4750706934088118880 | 200000 | 0.005996 | **0.006281** | 0.007300 | 0.007069 | 31.41 |

Ratio of medians C+/Rust = **6.25×**; the note attributes this to a mix of
allocator cost and the large optimisation-level difference. C+ includes
managed-runtime bookkeeping/mutex plus stdlib metadata record/forget; Rust's
`Vec` uses the system allocator with no first-party side table. A repeat while
other agents were active gave higher medians (C+ 0.051778 s, Rust 0.009557 s,
checksums still identical) — host-load sensitivity, canonical table unchanged.

---

## 4. Peak RSS (observed, not a safety metric)

Source: `benchmarks/safety_compare/MEMORY_USAGE.md`. Metric is the
`maximum resident set size` line of `/usr/bin/time -l` (macOS/BSD `time`) in
bytes; the separate `peak memory footprint` line was **not** used. Values are
multiples of 16384 bytes (16 KiB pages). Protocol: 1 warmup + 7 interleaved
round-robin runs per binary; median of 7. Workloads and checksums are those of
§2; all three variants matched per workload.

Exact primary measurement driver (from `MEMORY_USAGE.md` §Protocol):

```python
import subprocess, re, statistics, os
out='build/bench_safety'
workloads=['loop','nested','modern']; variants=['cplus_safe','cplus_legacy','rust']
bins={(v,w):f'{out}/{v}_{w}' for w in workloads for v in variants}
NRUNS=7
def measure(binpath):
    r=subprocess.run(['/usr/bin/time','-l',binpath],capture_output=True,text=True,timeout=300)
    m=re.search(r'(\d+)\s+maximum resident set size',r.stderr)
    return int(m.group(1)), r.stdout.strip()
for k,p in bins.items(): measure(p)                 # warmup, discarded
samples={k:[] for k in bins}; checksums={}
for rnd in range(NRUNS):
    for k,p in bins.items():
        rss,chk=measure(p); samples[k].append(rss); checksums[k]=chk
# report median/min/max per (workload,variant)
```

Median peak RSS (bytes; 1 MiB = 1,048,576 bytes):

| Workload | Variant | Median (bytes) | MiB | min (bytes) | max (bytes) |
|---|---|---:|---:|---:|---:|
| loop | C+ safe | 3,719,168 | 3.55 | 3,719,168 | 3,719,168 |
| loop | C+ legacy | 1,671,168 | 1.59 | 1,671,168 | 1,687,552 |
| loop | Rust `-O` | 1,851,392 | 1.77 | 1,851,392 | 1,851,392 |
| nested | C+ safe | 3,719,168 | 3.55 | 3,719,168 | 3,735,552 |
| nested | C+ legacy | 1,671,168 | 1.59 | 1,654,784 | 1,671,168 |
| nested | Rust `-O` | 1,851,392 | 1.77 | 1,851,392 | 1,851,392 |
| modern | C+ safe | 21,348,352 | 20.36 | 20,250,624 | 23,396,352 |
| modern | C+ legacy | 18,235,392 | 17.39 | 18,219,008 | 19,808,256 |
| modern | Rust `-O` | 14,188,544 | 13.53 | 14,188,544 | 14,188,544 |

Observed ratios: C+ safe / C+ legacy = 2.23× (loop), 2.23× (nested), 1.17×
(modern); Rust / C+ legacy = 1.11×, 1.11×, 0.78×. A second pass (1 warmup + 5
runs) reproduced loop/nested and C+ legacy/Rust medians to within one page;
`modern` C+ safe was variable (20,267,008 vs 21,348,352 bytes) — treat ~20–21
MiB as approximate. `loop`/`nested` RSS is process baseline (arithmetic folded),
`modern` is the only genuinely allocating workload, and the C+ and Rust
standard libraries are **different implementations**, so these are observations
of particular binaries, not a language ranking. Goose/plain C/Fil-C RSS:
**PENDING** (out of scope for that task; no toolchain for Fil-C).

---

## 5. Safety-outcome matrix

### 5.1 Per-case CVE-like outcomes (baseline hazard → safe outcome → classification)

Sources: `tests/scenarios/cve_like/NOTES_1.md` … `NOTES_5.md`. Apple clang 16's
ASan/TSan runtimes are broken on this host, so **baseline** sanitizer evidence
uses Homebrew clang 23.1.6 with the same `-fsanitize=…-isysroot …/MacOSX.sdk`
flags; safe variants use `/usr/bin/clang` (Apple clang 16). "Safe" outcomes are
defined refusals or bounded results at exit 0 (or documented exit 2 in NOTES_4),
in both the SafeCRT layer (`safe_c.h` + `libcplus_safecrt.a`) and the plain-libc
layer (`-DPLAIN_LIBC`, `cplus_libc_safe.h` + `libcplus_hardened.a`), unless noted.

| Case | Baseline hazard (tool) | Safe outcome | Classification / residual limit |
|---|---|---|---|
| 01 | 64→32 truncation, heap overflow (ASan exit 134 at `-O0`; dead store removed at `-O1`) | refuses count > UINT32_MAX; valid input ok | **IMPLEMENTED** (checked `calloc` + hardened memset); caller must validate truncated count |
| 02 | negative length → negative-size-param (ASan exit 134) | refuses negative length; valid length ok | **IMPLEMENTED**; caller must reject negative before cast |
| 03 | `free()` of forged pointer / bad realloc (ASan exit 134) | injected realloc failure preserves pointer+generation then ok; SIZE_MAX request refused | defined refusal; cannot prove *all* realloc failures; caller must check for `NULL` |
| 04 | read after `realloc` moved block (ASan exit 134) | stale alias refused by generation check; re-derived byte read ok | generation checks cover tracked blocks only; in-place realloc is conservatively "stale" |
| 05 | element-count product overflow (ASan exit 134) | refuses 32-bit product overflow; valid rows/cols ok | **PARTIAL** — library rejects wrapping allocation; raw pointer-index write still needs caller range check |
| 06 | aliased cache slot, UAF write (ASan exit 134) | stale read skipped, tracked allocation active | **NOT-FIXABLE-BY-LIBC** for aliasing/UAF; the fix is caller-side single ownership; library gives checked alloc + defence-in-depth liveness query |
| 07 | repeated session-token release, double-free (ASan exit 134) | repeat release no-op; checked re-release `REFUSED` | **PARTIAL** — checked API refuses repeat release; raw double `free(p)` remains UB; lexical fix caller-side |
| 08 | write to freed list node (ASan exit 134) | node unlinked and scrubbed while live; exit 0 | **NOT-FIXABLE-BY-LIBC** for raw post-free write; caller-side save-next/unlink discipline |
| 09 | user-sized VLA stack clash (ASan stack-overflow exit 134) | attack input exit 2 `rejected oversized window`; benign bounded use exit 0 | caller-side input validation + code-idiom change; libc covers only the heap window |
| 10 | length below header → oversized memcpy (ASan exit 134 at `-O0`) | refuses short length and oversized payload (ESNOSPC/ERANGE) | **IMPLEMENTED** (capacity-aware checked memcpy); caller must check `total < HEADER_SIZE` |
| 11 | arena `offset+bytes` wrap → misaligned store (ASan silent; UBSan finds misalignment) | attack exit 2 `refused by overflow-safe arena guard`; benign/odd offset exit 0 with `memcpy` | caller-side arithmetic fix (no addition); library check is defence-in-depth |
| 12 | 1-byte off-by-one heap copy (ASan exit 134 at `-O0`, silent at `-O1`) | attack exit 2 `bio refused (len >= 24)`; benign copy exit 0 | caller-side off-by-one fix; checked memcpy independently enforces the 24-byte bound |
| 13 | OOB function-pointer table dispatch (ASan rc 134; `cmd=99` → rc 132 SIGILL) | `cmd >= 3` rejected, exit 0, clean under ASan; in-range dispatch ok | **NOT-FIXABLE-BY-LIBC** — caller-side bound check; not CFI/shadow stack |
| 14 | cursor past ring capacity (ASan exit 134) | `cursor = (cursor + skip) % capacity` → in-range, exit 0 | caller-side modulo bound; valid only for fixed non-zero capacity |
| 15 | data race / UAF on global `active` (TSan rc 134) | mutex + stop flag + `pthread_join`; `resets=1` exit 0; 500 resets under TSan → 0 warnings | **NOT-FIXABLE-BY-LIBC** — caller-side synchronization |

Gate result (`tests/scenarios/cve_like/run_cve_like.sh`, quoted in `NOTES_5.md`):
`cve-like safe variants: 10 passed, 0 failed` with input `1` in both layers,
exit status 0. An earlier run before extra variants existed reported
`4 passed, 0 failed`. The table above also records cases whose fixes are
**caller-side** — notably cases 06–08 and 13/15 are explicitly *not* claimed to
be repaired by the library.

### 5.2 Suite-level outcome matrix (exact final lines; current collection)

Source: `docs/security/verification-status.md` §2, §6. Commands run serialized
from repo root, no `|| true` masking. **Aggregate: BLOCKED / NOT GREEN** — 5 of
6 suites exited non-zero; only the ABI suite is clean.

| Suite (exact command) | Exit | Final line / counts |
|---|---:|---|
| `sh tests/run_tests.sh` | 1 | `C+ tests: 99 passed, 51 failed` (all 51 = `compilation failed` from the §5.3 blocker) |
| `sh tests/libc/run_libc_tests.sh` | 1 | aborted at prerequisite `build_libc.sh` (`errno.h` not found, missing `MacOSX27.sdk`); no summary line |
| `sh tests/crt/unit/run_unit_tests.sh` | 1 | aborted at prerequisite `build_safecrt.sh` (`stdio.h` not found); no summary line |
| `sh tests/stdlib/run_deep_tests.sh` | 1 | `deep stdlib tests: 28 passed, 58 failed, 1 skipped (29 file(s) x 3 modes)`; all 58 = BUILD-FAIL from §5.3 |
| `sh tests/abi/run_abi_compat.sh` | **0** | `abi_compat: 69 passed, 0 failed` (only clean suite) |
| `sh tests/native/build_native_tools.sh` | 1 | `native tools: 1 built, 7 failed, 0 missing` |

Other sibling artifacts add the following outcomes:

- **Resource exhaustion** (`tests/security/resource_exhaustion/NOTES.md`): 8
  harnesses × 2 modes = **16 passed, 0 failed**, 0 build-failures/timeouts,
  1 documented in-test SKIP (`pthread_create` failure not injectable via
  `RLIMIT_NPROC` on this host). 78 checks × 2 modes, all 0 failures. Wall time
  `real 25.63 s`. UBSan instrumentation confirmed with `nm … | grep -c __ubsan`
  → 10 (vs 0 for strict-none).
- **Fuzz100** (`tests/fuzz100/RESULTS.md`): `pass=56, fail=9, build_failure=1,
  timeout=0, skipped=6, blocked=0` (72 discovered, 66 executed), runner exit 1.
- **Deep stdlib clean run** (`docs/stdlib/TASK6_FINAL_REPORT.md` §2): at an
  earlier state, `deep stdlib tests: 84 passed, 0 failed, 0 skipped (28 file(s)
  x 3 modes)`, exit 0. This is recorded evidence from a different HEAD/state;
  it does **not** contradict the blocked current run above, and neither result
  is silently substituted for the other.

### 5.3 Blockers affecting the safety evidence (verbatim, external)

- **External compiler/runtime interface (`CPLUS_EXIT_ARITHMETIC`)** — 109
  invocations attributable (51 + 58); the instrumented path compiles external
  `c-plus-core/runtime/cplus_runtime.c` against the local header whose enum
  ends at `CPLUS_EXIT_IO = 109`. Fails before any test code compiles, so it is
  **not** a finding against the fixtures. A build-only `-DCPLUS_EXIT_ARITHMETIC=110`
  shim was used for the resource-exhaustion UBSan evidence without editing any
  checked-in header or suppressing findings.
- **Host SDK** — default Homebrew clang 23.1.6 targets a missing `MacOSX27.sdk`;
  ABI suite avoids this by pinning `/usr/bin/clang`, Apple clang 16.
- **Native-tool source mismatch** — `stdlib/error.hp:223:30: error: use of
  undeclared identifier 'reporting'`, 7 failed native builds.

---

## 6. Sanitizer findings — reported separately, not folded into safety claims

1. **Backend support matrix** (recorded probe at `tests/stdlib/agent2/SANITIZER_MATRIX.md`):
   `none`, `undefined`, `integer` accepted; ASan, LeakSanitizer, TSan,
   MemorySanitizer and CFI **unsupported** by `/usr/bin/clang` — no pass claimed
   for those five. In that matrix (9 tests × 3 accepted configs = 27 runs), 23
   PASS and 4 FAIL under `--sanitize=integer`: genuine unsigned-overflow
   findings at `cplus_runtime.c:174:5` in `deep_collections_alloc`,
   `deep_collections_boundary`, `deep_string_task4`, `deep_vector_task4`
   (run exit 134). The matrix was captured at an earlier HEAD and **not
   re-executed** in the current collection; current deep UBSan builds are
   blocked by §5.3.
2. **Resource-exhaustion UBSan** (§5.2): with the build-only shim, 8/8 builds
   and 8/8 runs, empty stderr, zero UBSan diagnostics; instrumentation verified
   by symbol count. This is a real executed sanitizer pass, not a fabricated
   green.
3. **CVE-like baseline ASan/TSan** (§5.1): the crashes and races are genuine
   sanitizer findings on **original C files**, produced with Homebrew clang 23
   because Apple clang 16's sanitizer runtimes abort at startup on this host
   (`AddressSanitizer: CHECK failed: sanitizer_malloc_mac.inc:189 …`, TSan
   "interceptors not installed"). Safe variants re-checked under ASan/UBSan
   produced zero diagnostics. The Apple clang 16 runtime failure is a host
   issue, not a result about any program.
4. Sanitizer findings are **evidence about specific executions**, not a proof
   of memory safety, and are kept distinct from the speed/RSS measurements.

---

## 7. Artifact index (synthesis inputs)

| Artifact | Supplies | Status |
|---|---|---|
| `benchmarks/safety_compare/RESULTS.md` | speed medians, ratios, toolchain, honest caveats, Fil-C note | present |
| `benchmarks/safety_compare/MEMORY_USAGE.md` | peak RSS table, protocol, checksums, caveats | present |
| `benchmarks/safety_compare/bench_rust_NOTES.md` | Rust 31-run median/p95, build command, safety semantics | present |
| `benchmarks/safety_compare/METHODOLOGY.md` | protocol, p95-only-if-n≥20 rule, "same or closer" definition, non-claims, authoritative host record | present |
| `benchmarks/safety_compare/legacy_speed_NOTES.md`, `legacy_speed.c` | sanitizers-off C+ vs legacy C, n=31 medians/p95 (§3.5) | present |
| `benchmarks/safety_compare/ALLOC_COMPARE_NOTES.md`, `alloc_bench.{cp,rs}` | C+ `stdlib_alloc` vs Rust `Vec` medians/p95, checksum (§3.6) | present |
| `benchmarks/safety_compare/{modern,loop,nested}.{cp,c,rs,goose}`, `bench_modern.{cp,rs}`, `run_bench.sh` | workload sources / reproducibility | present |
| `benchmarks/stdlib/allocator/NOTES.md`, `results/allocator_bench.csv` | allocator medians/p95, tracking toggle, findings | present (CSV read) |
| `benchmarks/safety_compare/safety_matrix.cp`, `validate_results.cp`, `results/_probe/` | native safety-outcome matrix (C+ side) and probe artifacts | source present; results now recorded in `SAFETY_MATRIX_NOTES.md` / `VALIDATOR_NOTES.md` |
| `tests/scenarios/cve_like/NOTES_1.md` … `NOTES_5.md` | 15-case safety-outcome matrix, gate output | present |
| `docs/security/verification-status.md` | tool versions, suite matrix, blockers, sanitizer status | present |
| `tests/security/resource_exhaustion/NOTES.md` | 16/16 resource-exhaustion outcomes, SKIP | present |
| `tests/fuzz100/RESULTS.md` | fuzz100 pass/fail counts | present |
| `docs/stdlib/TASK6_FINAL_REPORT.md` | earlier clean deep-stdlib run | present |
| p95 for the 5-variant `loop`/`nested` rows and the 4-variant `modern` speed row | those tables record medians only (§3.5 p95 is a different build configuration) | **PENDING** |
| Native safety-outcome matrix results (`safety_matrix.cp` Rust side + runner output) | `SAFETY_MATRIX_NOTES.md`: observed `OK`/`REFUSED`/`ABORT`/`COMPILE_ERROR` tokens, both layers, with explicit non-equivalence note | present |
| `validate_results.cp` validation run output | `VALIDATOR_NOTES.md` records both pre-fix `BENCH_VALID 192 5` and fixed `BENCH_BLOCKED 5` + `BENCH_VALID 192 0`; independently rebuilt and verified here, exit 0 | present (fixed, verified) |
| Goose/plain-C/Fil-C peak RSS | out of scope / no toolchain | **PENDING** |
| Fil-C speed and safety results | no `filc` toolchain installed | **PENDING** |

---

## 8. What this proves / does not prove

**What this proves (within the recorded scope):**

- On this host and these synthetic workloads, the compared binaries computed
  the same checksums and their run times and peak RSS were **measured**, with
  exact commands, seeds, round counts, warmup discipline, tool versions and raw
  artifacts preserved. The four speed variants, the three RSS variants and the
  allocator variants are reproducible in configuration.
- The **fairest speed comparison** is C+ safe vs C+ legacy: identical source,
  identical `-O2`, identical stdlib layer; only safe-mode UBSan instrumentation
  differs. That overhead was measured at ~30× / ~3.9× (folded integer loops) and
  ~2.3× (allocation-heavy `modern` median); allocator tracking ON cost ~3.07×
  in the isolated toggle. These are observations on this machine.
- **With sanitizers disabled (§3.5), safe C+ showed no measurable overhead** on
  the two integer workloads: median ratios 1.020× (loop) and 1.039× (nested)
  vs legacy C at n=31, with p95 also recorded. `--emit-c` showed no hot-loop
  checks are inserted for these workloads. This corroborates that the large
  ratios in `RESULTS.md` are UBSan instrumentation effects, not a C+ safe-lowering
  cost. `METHODOLOGY.md` §5 defines "same or closer" strictly as identical
  checksums on every run plus similar medians within a stated band — never as
  equivalent safety.
- CVE-like cases show **defined refusals or bounded outcomes** for the tested
  inputs in both supported library layers, with a 10/0 gate; resource
  exhaustion passed 16/16; the ABI suite passed 69/0. The notes explicitly
  identify which repairs are caller-side rather than library-provided.

**What this does not prove — read literally:**

- **Comparable speed is measured, not guaranteed.** Timings are from one
  machine, a few synthetic workloads, at one point in time; material
  run-to-run and pass-to-pass variance is recorded. They are not a guarantee
  for any other workload, hardware, or load condition, and the non-instrumented
  loop timings are dominated by constant-folding, not loop execution.
- **No Rust-equivalence proof.** A matching checksum verifies one deterministic
  integer result; it says nothing about type systems, unsafe code, or "Rust is
  proven safe". Rust `Vec`/`String` and Goose dynamic arrays are different
  standard-library implementations — cross-checks, not apples-to-apples
  measurements of the C+ stdlib. Rust `-O` retains bounds-check semantics but
  may prove individual checks away; overflow checks are normally off in
  release; residual checks in machine code were **not** disassembled/counted.
  The allocator cross-check (§3.6) compares C+ at **-O0** against Rust at **-O**;
  its 6.25× ratio is explicitly not attributable to allocator design alone.
- **No full memory-safety proof.** C+ checked APIs are **runtime/API-boundary
  checks plus compile-time diagnostics**, which differ categorically from
  Rust's compile-time ownership/borrow checking. Cases 06–08, 13 and 15 are
  explicitly *not fixable by libc*: raw aliases, stale pointers, dispatch
  indices and thread races were repaired by caller-side code, not by a library
  that "made the pointer safe". LLVM/Clang is not itself a safety proof.
- **Sanitizer findings are separate.** ASan/TSan baseline crashes, the four
  `--sanitize=integer` unsigned-overflow findings, the broken Apple clang 16
  sanitizer runtimes, and the `CPLUS_EXIT_ARITHMETIC` blocker are recorded as
  their own evidence with their own toolchains. None is a speed/RSS result, and
  a blocked or unsupported sanitizer configuration is **not** counted as a
  pass. The current suite aggregate is **BLOCKED / NOT GREEN** (5/6 suites
  non-zero); an earlier clean deep run is recorded separately, not substituted.
- **RSS is an observation only.** `time -l` maximum RSS says nothing about
  correctness or safety; `modern` C+ safe RSS is explicitly approximate.
- **Fil-C was not measured** (no toolchain); its absence in any table is a
  limitation, not a zero or a failure. Plain C and Goose RSS are PENDING.
- The **native safety-outcome matrix** is now recorded in
  `SAFETY_MATRIX_NOTES.md`: C+ refusals come from *runtime* checked APIs (with a
  build-only `-DCPLUS_EXIT_ARITHMETIC=110` shim), while Rust rejects null/use-after-move
  at *compile time*. Its observed `OK`/`REFUSED`/`ABORT`/`COMPILE_ERROR` tokens
  are evidence of observed behaviour only, **not** of equivalent guarantees.
  `validate_results.cp` output: pre-fix `BENCH_VALID 192 5` is recorded in
  `VALIDATOR_NOTES.md`; the fixed validator was rebuilt and verified here as
  `BENCH_BLOCKED 5` + `BENCH_VALID 192 0`, exit 0 — blocked rows are reported
  separately, never as failures or passes.
- This is one repository at one branch state, built around one C+ driver and
  toolchain set. It is not a general performance, safety, or "language X is
  faster/safer" claim about any language or standard library.

## Final reproducible-driver run (after all integrations)

Command:

```sh
sh benchmarks/safety_compare/run_all.sh
```

Actual final result (verbatim from `results/summary.json`): `counts` =
`{"ok": 15, "blocked": 5, "fail": 0}`, `required_failures: 0`, `status:
"PASS"`, driver exit 0. The 5 `blocked` rows are recorded with
`"status":"blocked"` and `"reason":"external C+ runtime blocker:
CPLUS_EXIT_ARITHMETIC"`; they are **not** timing results and **not** failures.

Validator: `validate_results.cp` was rebuilt from current source and run on
`results/summary.json` with the verified output:

```
BENCH_BLOCKED 5
BENCH_VALID 192 0
exit=0
```

The pre-fix output was `BENCH_VALID 192 5` (exit 1), where the `5` counted the
externally blocked rows as `runs_ge_7` failures because their `runs_ok` is 0.
The earlier `BENCH_VALID 157 0` recorded in this document was from an even older
evidence file and is **superseded**.

**Validator blocked-row semantics (fixed and verified).** A record with
`"status":"blocked"` (or `"skipped"`) is an externally blocked row: it is exempt
from the runs>=7 and checksum rules, never contributes a failure, and is counted
separately on a `BENCH_BLOCKED <count>` line; only `"status":"ok"` (or records
with no status field) are validated strictly. Here the 5 blocked rows are
reported as `BENCH_BLOCKED 5` and excluded from failures, giving an exit-0
`BENCH_VALID 192 0` — a clean validation of the 15 measured rows, with the 5
external build blockers accounted for separately. Reproduction (from the repo
root):

```sh
timeout 30 build/c+ build benchmarks/safety_compare/validate_results.cp \
  --strict --sanitize=none -o build/validate_results
build/validate_results benchmarks/safety_compare/results/summary.json
echo "exit=$?"
```

Matched checksums: `loop=19999999900000000`, `nested=16000000`,
`modern=2200000200000`, `bench_modern=5595356797869224`,
`alloc_bench=4750706934088118880`.

All 15 `ok` rows from the final table (`results/table.txt`), medians and p95
quoted verbatim (p95 by linear interpolation, `runs=7`, `warmup=1` per the
`summary.json` `method` field). The 5 `cplus_safe` rows are the blocked rows
listed above and carry no median/p95 (`-`).

| Workload | Variant | median (s) | p95 (s) | status |
|---|---|---:|---:|---|
| alloc_bench | Rust -O | 0.010404 | 0.013873 | ok |
| alloc_bench | C+ legacy | 0.039720 | 0.049571 | ok |
| bench_modern | Rust -O | 0.023018 | 0.026586 | ok |
| bench_modern | C+ legacy | 0.020533 | 0.024590 | ok |
| legacy_speed/loop | C `-O2` | 0.002890 | 0.003327 | ok |
| legacy_speed/nested | C `-O2` | 0.004658 | 0.008925 | ok |
| loop | C `-O2` | 0.005660 | 0.006696 | ok |
| loop | Rust -O | 0.005310 | 0.008098 | ok |
| modern | Rust -O | 0.009449 | 0.011401 | ok |
| modern | C+ legacy | 0.008306 | 0.010272 | ok |
| nested | C `-O2` | 0.004812 | 0.009405 | ok |
| nested | Rust -O | 0.003341 | 0.006325 | ok |
| safety_matrix | Rust -O | 0.005170 | 0.014102 | ok |
| safety_matrix/matrix | C+ legacy | 0.006687 | 0.014287 | ok |
| validate_results/…/evidence.csv | C+ legacy | 0.004316 | 0.008333 | ok |

The 5 C+ safe rows (`alloc_bench`, `bench_modern`, `modern`,
`safety_matrix/matrix`, `validate_results/…/evidence.csv`) are `blocked` by the
external `CPLUS_EXIT_ARITHMETIC` runtime/header mismatch: their safe-mode builds
exit 1 while compiling the instrumented C+ runtime, before benchmark code is
reached. They are **externally blocked build attempts, not timing results** —
no median, p95 or checksum exists for them and none is claimed. The 15 `ok` rows
above are the complete set of measured results from this driver run. Earlier
direct `--strict --sanitize=none` measurements are recorded in the per-tool
NOTES files and must not be presented as sanitizer results.

Non-claims for this driver run, stated explicitly:

- **No Rust equivalence.** Matching checksums (e.g. `bench_modern=5595356797869224`)
  verify one deterministic integer result per workload; they say nothing about
  type systems or equivalent safety guarantees. C+ checked APIs are
  runtime-enforced; Rust rejects several of the same errors at compile time.
- **No memory-safety proof.** These are throughput observations from one host
  and synthetic workloads, not evidence that any implementation is memory-safe.
- **No sanitizer-as-proof.** The blocked safe-mode builds attempted UBSan
  instrumentation but never produced a binary; an unsupported or blocked
  sanitizer configuration is not counted as a pass. A clean sanitizer run, where
  one exists, is evidence about a specific execution, not a proof.
