# PHASE 8 — measured performance + real ABI verification

Agent I (`agent/modern-stdlib`), 2026-11-23T15:01Z. All numbers below were
produced by the exact commands in `benchmarks/phase8/run.sh` and are kept raw in
`benchmarks/phase8/results/`, `benchmarks/phase8/logs/` and
`benchmarks/phase8/abi-*.log`. **No number is estimated, extrapolated or
fabricated.** Where a benchmark could not run, the exact error is recorded
instead of a result.

## 0. Environment and protocol

| Item | Value |
|---|---|
| Host | `Darwin 27.2.0 arm64`, `sysctl -n machdep.cpu.brand_string` = `Apple M2` |
| Compiler | `/usr/bin/clang` = Apple clang 16.0.0 (`clang-1600.0.26.6`) |
| C+ driver | `./build/c+` = `C+ 0.1.0` |
| rustc | `1.100.0 (0ef1bc386 2026-11-09)` (not used for new timings) |
| Branch / HEAD | `agent/modern-stdlib`, `ab8cb51` |
| Wall-clock tool | `date +%s%N` (nanosecond timestamps) around each process |
| Wall-clock protocol | 1 untimed warmup per binary, then 9 interleaved (round-robin) measured rounds; median/min/max over the 9 |
| In-process protocol | `benchmarks/stdlib/allocator/*` measure with `clock_gettime(CLOCK_MONOTONIC)` and already use 1–2 warmups + 9–11 rounds |
| Concurrency caveat | Other agents were building and testing on this shared host; current timings are generally higher than the historical single-owner runs. Ratios are indicative, not controlled benchmarks. |

`bash` in this environment resolves to `/opt/homebrew/bin/bash` (GNU bash
5.3.15), so the required `bash tests/abi/run_legacy_labs.sh` ran under bash 5,
not 3.2; the documented 3.2 fallback was not needed.

All C+ binaries were built with `--strict --sanitize=none` or
`--release --legacy-c` because the default instrumented C+ runtime currently
fails to compile (see §2.6). This is a substitution relative to some historical
"safe mode" numbers and is called out wherever it matters.

---

## 1. Performance — current measurements and before/after

### 1.1 Allocation overhead: `raw` (host malloc), `tracked` (cplus_malloc), `query` (tracked + debug queries)

Build (outputs kept under `benchmarks/phase8/bin/`):

```sh
CC=/usr/bin/clang; ROOT=$PWD; INC=$ROOT/libc/install-include; LIB=$ROOT/build/libc/libcplus_hardened.a
$CC -O2 -std=gnu17 -I"$INC" benchmarks/stdlib/allocator/allocator_bench.c "$LIB" -lm -pthread -o benchmarks/phase8/bin/allocator_bench
./benchmarks/phase8/bin/allocator_bench --runs 9 --warmup 1 --seed 314159265
```

Raw CSV: `benchmarks/phase8/results/allocator_current.csv`. "Before" is the
checked-in canonical CSV `benchmarks/stdlib/allocator/results/allocator_bench.csv`
(recorded by commit `ec8f2e2`, 11 runs / 2 warmups, same seed and ops). The
earlier git revision `664af8b` also holds a 9-run/2-warmup CSV; notable there:
`grow` median `0.000916 s` versus `0.125590 s` at `ec8f2e2`, i.e. that workload
was redefined between commits, so `grow` is **not** a like-for-like before/after.

| Workload | Variant | ops | before median (s) | **current median (s)** | current min | current max | current/before |
|---|---|---:|---:|---:|---:|---:|---:|
| alloc | raw | 200000 | 0.004857 | **0.003853** | 0.002099 | 0.006534 | 0.79× |
| alloc | tracked | 200000 | 0.044537 | **0.037606** | 0.019287 | 0.047615 | 0.84× |
| alloc | query | 200000 | 0.045455 | **0.051312** | 0.037098 | 0.102613 | 1.13× |
| free | raw | 200000 | 0.001783 | **0.003332** | 0.001459 | 0.008218 | 1.87× |
| free | tracked | 200000 | 0.276622 | **0.378373** | 0.294687 | 0.543431 | 1.37× |
| free | query | 200000 | 0.250755 | **0.429178** | 0.329640 | 0.702423 | 1.71× |
| realloc | raw | 200000 | 0.016636 | **0.023670** | 0.014160 | 0.045978 | 1.42× |
| realloc | tracked | 200000 | 0.019575 | **0.034096** | 0.024771 | 0.136545 | 1.74× |
| realloc | query | 200000 | 0.024689 | **0.053382** | 0.034258 | 0.116551 | 2.16× |
| small | raw | 200000 | 0.007001 | **0.009907** | 0.006058 | 0.017860 | 1.42× |
| small | tracked | 200000 | 0.016342 | **0.022970** | 0.019555 | 0.048322 | 1.41× |
| small | query | 200000 | 0.019500 | **0.032349** | 0.021692 | 0.045999 | 1.66× |
| large | raw | 4000 | 0.000675 | **0.001101** | 0.000431 | 0.002031 | 1.63× |
| large | tracked | 4000 | 0.000829 | **0.001459** | 0.000596 | 0.006602 | 1.76× |
| large | query | 4000 | 0.000957 | **0.002096** | 0.000669 | 0.006045 | 2.19× |
| grow | raw | 200000 | 0.125590 | **0.202054** | 0.137127 | 0.345354 | 1.61× |
| grow | tracked | 200000 | 0.141173 | **0.204274** | 0.154953 | 0.268606 | 1.45× |
| grow | query | 200000 | 0.141505 | **0.238401** | 0.155256 | 0.317438 | 1.68× |

The `tracked/raw` median ratios measured now (e.g. alloc 9.76×, small 2.32×,
free 113.6×, grow 1.01×) are consistent with the documented finding that the
free path is dominated by the saturated `g_recent[1024]` probe, not a constant
cost. The `query − tracked` delta remains the clean isolation of debug-query
cost (e.g. small +9.38 ms / 200k pairs ≈ +47 ns per pair).

### 1.2 Memory-tracking overhead: tracked vs untracked from one source

`benchmarks/stdlib/allocator/tracking_toggle.c` differs only by
`-DCPLUS_MALLOC_TRACKING`. Two binaries were built into `phase8/bin/` with the
same `/usr/bin/clang -O2 -std=gnu17` command and each was invoked 7 times;
each invocation itself performs 2 warmups + 11 in-process rounds. Raw:
`benchmarks/phase8/results/tracking_toggle_current.txt`.

| Build | median of 7 run-medians (s) | min | max | ns/op (at that median) | before (commit `ec8f2e2`, canonical single run) |
|---|---:|---:|---:|---:|---:|
| tracking **OFF** (host malloc/free) | **0.006169** | 0.003706 | 0.006785 | 30.85 | median 0.004004, ns/op 20.02 |
| tracking **ON** (cplus_malloc/free + side table) | **0.015983** | 0.011699 | 0.019754 | 79.91 | median 0.012287, ns/op 61.44 |

Current ON/OFF ratio of medians = **2.59×** (before: 3.07×). The same
process-to-process variance noted in `benchmarks/stdlib/allocator/NOTES.md` is
visible here; no single number should be read as a precise cost.

### 1.3 String append/growth and vector growth (2M workload)

Sources: `benchmarks/safety_compare/modern.cp` (real `stdlib/string.hp` +
`stdlib/vector.hp`) and `benchmarks/safety_compare/bench_modern.cp` (real string
+ checked-allocator growable vector with internal phase timings). Commands:

```sh
PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin ./build/c+ build --release --legacy-c benchmarks/safety_compare/modern.cp -o benchmarks/phase8/bin/modern_legacy
PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin ./build/c+ build --strict --sanitize=none benchmarks/safety_compare/bench_modern.cp -o benchmarks/phase8/bin/bench_modern_strict
sh benchmarks/phase8/time_runs.sh 9 modern_legacy=benchmarks/phase8/bin/modern_legacy bench_modern_strict=benchmarks/phase8/bin/bench_modern_strict
```

Both builds exited 0. All 9 runs of each binary printed the same checksum
(`2200000200000` and `5595356797869224`). Wall-clock results (raw:
`benchmarks/phase8/results/string_vector_wall.csv`):

| Binary | median (s) | min (s) | max (s) | before median (s) | current/before |
|---|---:|---:|---:|---:|---:|
| `modern --release --legacy-c` | **0.014634** | 0.010355 | 0.020249 | 0.0061 (`benchmarks/safety_compare/RESULTS.md`, 7 interleaved) | 2.40× |
| `bench_modern --strict --sanitize=none` | **0.083064** | 0.059302 | 0.130518 | 0.0616 (`bench_modern_NOTES.md` §5, 11 interleaved) | 1.35× |

Internal phase timings from `bench_modern_strict` over the same 9 runs
(microseconds; before = `bench_modern_NOTES.md` §6 strict medians, 11 runs):

| Phase | current min | **current median** | current max | before median | current/before |
|---|---:|---:|---:|---:|---:|
| vector_us (2M push_back + at) | 38807 | **64988** | 96426 | 45321 | 1.43× |
| string_us (200k append) | 6470 | **8533** | 20259 | 7435 | 1.15× |
| total_us | 45277 | **74554** | 112729 | 53348 | 1.40× |

These are the same workloads and the same build flags as the historical notes;
the absolute increase is consistent with host load from parallel agents, not a
demonstrated regression in the headers (no header was changed by this task).

### 1.4 File I/O and JSON parsing (existing `tests/stdlib/bench_extra` harness)

No new benchmark source was written: `tests/stdlib/bench_extra.c` (yyjson +
`fopen`/`fread` + `malloc`/`free`) and `tests/stdlib/bench_extra.cp`
(`stdlib/json.hp`, `stdlib/file.hp`, `cplus_alloc`/`cplus_release`) already do
identical work. Both were rebuilt at `-O2` / `--release --strict --sanitize=none`
and timed with 1 warmup + 9 interleaved rounds. Checksum `26648` matched on all
18 runs. Raw: `benchmarks/phase8/results/extra_compare_wall.csv` and
`benchmarks/phase8/logs/extra_*.run.*.out`.

| Harness / phase | plain C `-O2` min / **median** / max (ms) | C+ `-O2`, sanitizer-free min / **median** / max (ms) | C+/C median |
|---|---|---|---:|
| json_ms (20k parses) | 3.430 / **6.428** / 8.144 | 5.373 / **9.087** / 11.520 | 1.41× |
| file_ms (200 opens + reads) | 2.221 / **3.708** / 6.698 | 2.185 / **2.365** / 8.691 | 0.64× |
| alloc_ms (200k alloc/free) | 0.023 / **0.024** / 0.052 | 22.416 / **23.478** / 44.324 | 978× (see caveat) |

Wall clock for the whole binary: plain C median 20.213 ms (min 14.712, max
52.210), C+ median 48.907 ms (min 40.217, max 84.286) — 2.42×.

**Caveat on alloc_ms:** the plain-C loop writes `block[0]` and sums it, so
`-O2` can satisfy the sum without the allocation; 0.024 ms for 200k malloc/free
pairs is not a believable allocator cost and this ratio must **not** be reported
as "C+ allocation is 978× slower". The tracked/untracked isolation in §1.1–1.2
is the valid allocator measurement.

**Before/after for this harness:** no historical measured json/file numbers
exist in the checkout. `tests/stdlib/perf_gate.sh` records *thresholds*
(`T_JSON=3.0`, `T_FILE=3.0`, `T_ALLOC=6.0`, safe-mode `-O0` + sanitizers, single
run), not measured ratios; our sanitizer-free `-O2` ratios are not comparable to
those thresholds. `docs/MODERN_STDLIB.md` §9 records only vector/string numbers,
not JSON/file.

### 1.5 Vector/string 200k workload vs `docs/MODERN_STDLIB.md` §9

Same sources `tests/stdlib/bench_plain.c` / `bench_compare.cp`, built as C
`/usr/bin/clang -O2` and C+ `./build/c+ build --release --strict --sanitize=none`.
Checksum `20000120000` matched on all 18 runs. 1 warmup + 9 interleaved rounds;
internal medians (ms):

| Workload | current plain C | current C+ | before plain C (`docs/MODERN_STDLIB.md`, release `-O2`, best of 3) | before C+ (same) |
|---|---:|---:|---:|---:|
| `vector` 200k push + read | **0.374** (min 0.219, max 0.453) | **0.479** (min 0.401, max 0.730) | 0.21 | 1.33 |
| `string` 20k construct + append | **0.838** (min 0.387, max 1.050) | **6.818** (min 4.801, max 11.768) | 0.37 | 2.10 |

Not like-for-like with the documented "release" rows: those used plain
`--release` (which retains sanitizer instrumentation by design) and best-of-3;
these use `--strict --sanitize=none` and median-of-9, on a host under load. The
numbers are reported, not interpreted as a header-level regression.

### 1.6 Benchmarks that could NOT run (exact errors)

1. `PATH=... CPLUS_CLANG=/usr/bin/clang sh tests/stdlib/perf_gate.sh`
   → `EXIT_CODE=1`. The safe-mode C+ build aborts before any measurement:
   ```
   /Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/cplus_runtime.c:360:23: error: use of undeclared identifier 'CPLUS_EXIT_ARITHMETIC'
   error: failed to compile instrumented C+ runtime
   ```
   (also `CPLUS_EXIT_CONVERSION` at lines 294/347). This is the documented
   external-runtime blocker, not a performance result. Full log:
   `benchmarks/phase8/logs/perf_gate.log`.
2. Safe-mode builds of the C+ benchmark harnesses (`build/c+ build --release`
   without `--sanitize=none`) hit the same blocker; this is why §1.3–1.5 use
   `--legacy-c` or `--strict --sanitize=none`.
3. In-repo discrepancy found: `benchmarks/safety_compare/RESULTS.md` describes
   the then-authoritative `results/summary.json` as `ok=15 blocked=5 fail=0`,
   but the file currently checked in (`generated_at_utc` =
   `2026-11-23T06:20:30Z`) records `ok=9 blocked=5 fail=6`; the 6 Rust rows have
   `build_exit: 127` (`rustc` not found under the script's pinned `PATH`) and
   `results/table.txt` marks them `fail`. Those rows are not reused here as
   historical numbers.

---

## 2. ABI verification — exact commands, exits and final lines

### 2.1 `sh tools/abi_compare/run.sh compare tests/abi/fingerprints/baseline.txt`

`EXIT_CODE=0`. Final lines:
```
abi_fp: wrote tools/abi_compare/.out/compare/fingerprint.txt (1238 records) and fingerprint.json
abi_fp: ABI fingerprint MATCHES baseline (tests/abi/fingerprints/baseline.txt).
abi_fp: diff is empty: tools/abi_compare/.out/compare/diff.txt
```
`diff.txt` is 0 lines. Log: `benchmarks/phase8/abi-1-fingerprint-compare.log`.

### 2.2 `CPLUS_CLANG=/usr/bin/clang sh tests/abi/run_abi_compat.sh`

`EXIT_CODE=0`. Final line: `abi_compat: 69 passed, 0 failed`. Zero `FAIL` lines.
Per-probe final lines (from `build/abi/compat/*.out`):
```
abi_compat_pointers: 20/20 pointer ABI checks match
abi_compat_layouts: 80/80 layouts match
abi_compat_c_driver: 13/13 C->C+ boundary checks match
abi_compat_cplus_caller: 10/10 C+->C boundary checks match
```
Log: `benchmarks/phase8/abi-2-compat.log`.

### 2.3 `CPLUS_CLANG=/usr/bin/clang sh tests/abi/run_abi_layouts.sh`

`EXIT_CODE=0`. Final line: `abi_layouts: 70/70 layouts match`. Full log:
```
clang: warning: build/libc/libcplus_hardened.a: 'linker' input unused [-Wunused-command-line-argument]
clang: warning: build/safe/libcplus_safecrt.a: 'linker' input unused [-Wunused-command-line-argument]
abi_layouts: 70/70 layouts match
```
`build/abi/host.out` first line:
`abi_layouts: baseline sizeof(void*)=8 sizeof(size_t)=8 sizeof(unsigned long)=8`.
(The `CPLUS_CLANG=/usr/bin/clang` pin is required here: ambient `clang` is
Homebrew clang 23.1.6, and `docs/stdlib/container_safety/ABI_PERFORMANCE.md`
records that the unpinned script fails before the ABI checks with
`fatal error: 'errno.h' file not found`. That unpinned failure was not re-run
in this task.)

### 2.4 `bash tests/abi/run_legacy_labs.sh`

`EXIT_CODE=0`. Final two lines:
```
run_legacy_labs: total=54 pass=54 fail=0 build_failure=0 timeout=0 skipped=0 blocked=0
required_failures=0  summary=/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/abi-labs/summary.json  logs=/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/abi-labs/logs
```
All 54 labs `PASS`. Log: `benchmarks/phase8/abi-4-legacy-labs-bash32.log`.

### 2.5 `sh tests/sdk/run_sdk_consumer.sh`

`EXIT_CODE=1`. First line: `sdk consumer: FAIL: libc build failed`. Exact link
error from the captured log:
```
duplicate symbol '_cplus_exp10' in:
    .../CMakeFiles/cplus_hardened_libc_shared.dir/src/math/math_c23.c.o
    .../CMakeFiles/cplus_hardened_libc_shared.dir/src/math/math_c23_musl.c.o
duplicate symbol '_cplus_exp10l' in: ...
duplicate symbol '_cplus_exp10f' in: ...
ld: 3 duplicate symbols
clang: error: linker command failed with exit code 1 (use -v to see invocation)
```
The configure log shows this run used `The C compiler identification is Clang 23.1.6`
(`/opt/homebrew/opt/llvm/bin/clang`) — the ambient compiler, not Apple clang.
With the script's supported override, the same script passes:

`CPLUS_CLANG=/usr/bin/clang sh tests/sdk/run_sdk_consumer.sh` → `EXIT_CODE=0`,
final line `sdk consumer: ok`. Log:
`benchmarks/phase8/abi-5b-sdk-consumer-appleclang.log`.

This is a build-toolchain failure in the SDK **shared-library** link, not a
layout/symbol ABI mismatch; no ABI-drift conclusion is drawn from it, and no
live-game success is claimed.

### 2.6 Confirmations requested by the task

| Confirmation | Evidence actually run |
|---|---|
| `sizeof(void *) = 8` | `build/abi/host.out` first line: `sizeof(void*)=8 sizeof(size_t)=8 sizeof(unsigned long)=8`; fingerprint record `L size void *  8`, `L align void *  8` |
| Struct layouts unchanged | `abi_layouts: 70/70 layouts match`; compat `abi_compat_layouts: 80/80 layouts match`; fingerprint diff empty (1238 records), including all `L size/align/offset` records |
| Function signatures unchanged | Fingerprint contains 12 `F signature ...` records (e.g. `cplus_malloc void *(size_t)`, `cplus_free void(void *)`, `cplus_strcpy_s errno_t(char *, rsize_t, const char *)`, `cplus_thrd_create int(cplus_thrd_t *, cplus_thrd_start_t, void *)`); all match baseline, diff empty; compile-time signature checks in `tools/abi_compare/abi_fp_probe.c` passed under `-Werror` |
| C interop | `abi_compat_c_driver: 13/13 C->C+ boundary checks match` and `abi_compat_cplus_caller: 10/10 C+->C boundary checks match` (§2.2); legacy labs `c_to_cp PASS`, `cp_to_c/cp_to_c PASS` (§2.4) |
| Precompiled C library compatibility | `PASS T3b_c_part_compile`, `PASS T3b_c_part_archive`, `PASS T3b_direct_link_probe`, `PASS T3b_cplus_caller_build`, `PASS T3b_cplus_caller_summary` — C+ driver linked the precompiled C archive directly (§2.2); legacy labs `precompiled/precompiled_lab PASS` (§2.4) |
| Exported symbols are C names, not mangled | `PASS T5_no_cxx_mangling` for all three `nm` files; symbol allow-list checks passed (§2.2) |

**Explicit statement:** ABI preservation is **not** claimed from compilation
success alone. The claim rests only on the executed probes above: the empty
fingerprint diff over layouts, enum values, compiler ABI defines and exported
symbols; the 70/70 and 80/80 layout checks; the 12 compile-time signature
checks; and real C↔C+ and precompiled-archive link-and-run tests. A clean build
by itself would not establish any of those properties.

---

## 3. Limitations

- All ABI results are **LP64 arm64 / Apple clang 16** only. No Windows x86
  layout, packing, calling-convention or ordinal evidence is produced here; no
  DLL/live-game success is claimed.
- Every timing is one host, one point in time, with other agents active. The
  protocol is reproducible; the exact nanoseconds are not.
- The safe (UBSan) C+ configuration remains blocked by the external runtime
  (`CPLUS_EXIT_ARITHMETIC` / `CPLUS_EXIT_CONVERSION`); no sanitizer-mode speed
  number is reported.
- `benchmarks/safety_compare/RESULTS.md` and the currently checked-in
  `results/summary.json` disagree (§1.6); this task reports the discrepancy
  rather than silently preferring one.
- No source, header, test, build script or git index entry was modified by this
  task. Files created are only under `benchmarks/phase8/`.
