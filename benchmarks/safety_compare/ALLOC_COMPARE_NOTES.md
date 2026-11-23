# Allocation-throughput comparison: C+ (stdlib allocator) vs Rust (measured 2026-11-23)

## Files (all under `benchmarks/safety_compare/`)

- `alloc_bench.cp` — C+ workload using the supported stdlib allocation path.
- `alloc_bench.rs` — Rust workload using `Vec<u8>` allocations.
- `ALLOC_COMPARE_NOTES.md` — this file (commands, environment, actual results).

## Why this allocation API

C+ uses `stdlib_alloc` / `stdlib_release` from `stdlib/error.hp`. Those wrappers
call the runtime's `cplus_alloc` / `cplus_release`, which live in
`libcplus_runtime.a` — the archive the C+ driver already links into every
executable. **No extra archive linking is needed, and there was no build error:**
the prescribed `c+ build --strict --sanitize=none` command succeeded with an
empty stderr. This is the same supported path the checked containers in
`stdlib/vector.hp` and `stdlib/string.hp` use.

The alternative `cplus_malloc` family (`libc/install-include/cplus_libc.h`)
needs `build/libc/libcplus_hardened.a`, which the C+ driver cannot link:
`benchmarks/stdlib/allocator/NOTES.md` records the duplicate-symbol failure
(`duplicate symbol '_cplus_realloc'` between `libcplus_hardened.a` and
`libcplus_runtime.a`). That documented limitation is why the hardened-libc
allocator was **not** used here; nothing in this file is inferred from an
untried build.

## Workload definition (identical operation count and checksum)

`N = 200,000` iterations. Each iteration performs **exactly one allocation and
one free**:

```
sz      = 8 + (i mod 8) * 8          /* 8, 16, ..., 64 bytes */
byte    = (i * 31 + 7) & 0xff
p       = stdlib_alloc(sz)           /* C+  |  vec![0u8; sz] in Rust */
sink    = p                          /* volatile global | black_box(&p) */
checksum = checksum * 33 + sz + byte  /* mod 2^64, value-based */
stdlib_release(p)                    /* C+  |  drop(p) in Rust */
```

- The `volatile` sink / `black_box(&p)` makes each allocation observable so the
  compiler cannot elide the alloc/free pair. Neither is read.
- The checksum contains **no pointer values**, so it is language-independent.
  Both binaries printed `checksum=4750706934088118880` on every run (1 warmup +
  11 measured runs each), and an independent Python recomputation produced the
  same value. Identical checksums confirm semantics matched.
- Only the loop is timed (`clock_gettime(CLOCK_MONOTONIC)` in C+,
  `std::time::Instant` in Rust); startup and the final `printf` are outside it.

## Exact commands (from the repository root)

Build C+ — note the backend command line was inspected with `--verbose`:
`-std=gnu17`, `-O0`, `-g`, `-pthread`, `-DCPLUS_SAFE=1`, no sanitizers, and
`libcplus_runtime.a` from `../c-plus-core/build-verify-1123/`:

```sh
mkdir -p build/bench_safety
PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin \
  build/c+ build --strict --sanitize=none \
  benchmarks/safety_compare/alloc_bench.cp \
  -o build/bench_safety/cplus_alloc_bench
```

Build Rust:

```sh
rustc -O benchmarks/safety_compare/alloc_bench.rs \
  -o build/bench_safety/rust_alloc_bench
```

Both builds exited 0 with no warnings. Run each binary once as an untimed
warmup (discarded), then 11 interleaved measured rounds (round-robin across
variants so they share CPU-frequency windows), every invocation wrapped in
`/usr/bin/timeout 60`:

```sh
python3 - <<'PY'
import subprocess, statistics
bins = {
    'cplus': 'build/bench_safety/cplus_alloc_bench',
    'rust':  'build/bench_safety/rust_alloc_bench',
}
RUNS = 11
def run(v):
    r = subprocess.run(['/usr/bin/timeout', '60', bins[v]],
                       capture_output=True, text=True, check=True)
    f = dict(kv.split('=', 1) for kv in r.stdout.strip().split())
    return int(f['checksum']), float(f['elapsed_s'])
for v in bins:
    run(v)                                  # warmup, discarded
samples = {v: [] for v in bins}
checks  = {v: set() for v in bins}
for rnd in range(RUNS):
    for v in bins:                          # interleaved round-robin
        cs, dt = run(v)
        checks[v].add(cs)
        samples[v].append(dt)
print('variant,checksum,runs,min_s,median_s,max_s,p95_s')
for v in bins:
    s = sorted(samples[v])
    p95 = s[int(0.95 * (len(s) - 1))]       # nearest-rank
    print(f'{v},{next(iter(checks[v]))},{len(s)},'
          f'{s[0]:.9f},{statistics.median(s):.9f},{s[-1]:.9f},{p95:.9f}')
print('checksums_equal=',
      checks['cplus'] == checks['rust'] and all(len(c) == 1 for c in checks.values()))
PY
```

## Environment

- Host: `Darwin 27.2.0`, `arm64`, Apple **M2**, macOS.
- C+ driver `C+ 0.1.0`, backend Apple clang 16.0.0 (`clang-1600.0.26.6`,
  `/usr/bin/clang`), mode `--strict --sanitize=none`, **`-O0`** (no `--release`
  was passed, so the driver's default debug optimisation applies; no UBSan).
- rustc `1.100.0` (`0ef1bc386 2026-11-09`), `-O`.
- `timeout` is `/usr/bin/timeout` (present on this host).

## Measured results (ACTUAL run; 1 warmup + 11 measured rounds each)

p95 is nearest-rank (element at index `floor(0.95 * (n-1)) = 9` of the sorted
11 samples). Raw samples are listed below the table. **No estimated or
fabricated numbers** — every value is copied from the harness output.

| Variant | checksum | ops | min (s) | **median (s)** | max (s) | p95 (s) | median ns/op |
|---|---|---:|---:|---:|---:|---:|---:|
| C+ `stdlib_alloc`/`stdlib_release` (`--strict --sanitize=none`, -O0) | 4750706934088118880 | 200000 | 0.033627000 | **0.039260000** | 0.049478000 | 0.047966000 | 196.30 |
| Rust `Vec<u8>` (`-O`) | 4750706934088118880 | 200000 | 0.005996458 | **0.006281417** | 0.007300375 | 0.007068959 | 31.41 |

Ratio of medians: C+ / Rust = **6.25x**.

Raw `elapsed_s` samples (round order, seconds):

```
cplus: 0.047966 0.039260 0.039844 0.033627 0.034519 0.034543 0.040966 0.038039 0.042659 0.049478 0.034703
rust:  0.006281417 0.006484125 0.006104625 0.006110792 0.005996458 0.007300375 0.006096667 0.006310291 0.006189709 0.007068959 0.006773542
```

The harness printed `checksums_equal= True`.

## Honest interpretation and limitations

1. **This is not an apples-to-apples allocator benchmark.** The prescribed
   build flags put C+ at `-O0` (strict, unsanitized) and Rust at `-O`. The
   6.25x median gap therefore mixes allocator cost with a large
   compiler-optimisation difference. A fairer follow-up would add a C+
   `--release --strict --sanitize=none` (-O2) variant; that variant was **not
   measured here**, so no number for it is claimed.
2. The C+ side includes the managed runtime's allocation bookkeeping/mutex
   (`cplus_alloc`) plus the stdlib metadata record/forget in
   `stdlib_alloc`/`stdlib_release`. Rust's `Vec` uses the system allocator with
   no first-party side table. Both are real implementations; the numbers
   describe these two configurations on this host, not a general language
   ranking.
3. `black_box(&p)` / `volatile g_sink` only pin the allocation; they are not
   reads of the data, so the loop is dominated by allocate/free work and the
   checksum arithmetic, not by memory traffic.
4. Variance is visible (C+ spread 0.0336–0.0495s, Rust 0.0060–0.0073s), so the
   median and p95 are the meaningful statistics. Interleaving limits drift but
   does not eliminate host noise. A repeat of the identical harness while other
   agents were active on the same host produced higher medians (C+ 0.051778s,
   Rust 0.009557s; checksums still identical) — further evidence that these are
   host-load-sensitive measurements. The table above remains the canonical
   first run; the repeat pass is reported here, not substituted into it.
5. Every invocation completed well inside `timeout 60`; no run was killed or
   retried. One machine, one point in time — configuration is reproducible,
   exact timings are not guaranteed elsewhere.
