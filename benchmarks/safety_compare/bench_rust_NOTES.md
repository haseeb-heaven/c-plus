# Rust `bench_modern` — build, measurement, and honest caveats

Source: `benchmarks/safety_compare/bench_modern.rs`, the Rust counterpart of the
coordinated C+ harness `benchmarks/safety_compare/bench_modern.cp` (deterministic,
PRNG-driven modern-stdlib vector/string workload). This file records only the
Rust side; the C+ side is documented by the agent that owns `bench_modern.cp`.

## Workload equivalence (checked against `bench_modern.cp`)

| C+ (`bench_modern.cp`) | Rust (`bench_modern.rs`) |
|---|---|
| `#define N 2000000`, `M 200000` | `const N: u32 = 2_000_000`, `M: u64 = 200_000` |
| `BENCH_SEED 0x9E3779B9u`, xorshift32 `next_u32()` | identical `BENCH_SEED` and `Xorshift32::next_u32()` transition (`x ^= x<<13; x ^= x>>17; x ^= x<<5`) |
| `N` × `v.push_back((int)(next_u32() & 0x7FFFFFFF))` | `N` × `Vec<i32>::push((next_u32() & 0x7FFF_FFFF) as i32)` |
| `N` × `vsum += v.at(next_u32() % N)` | `N` × `vsum += v[(rng.next_u32() % N) as usize]` |
| `string s = string("a")` before timing | `String::from("a")` before timing |
| `M` × random `r = next_u32()%4 + 1` lowercase chars appended; `ssize_sum += s.size()` | `M` × same PRNG-driven 1..4-char chunk appended via `push_str`; `ssize_sum += s.len()` |
| `checksum = vsum ^ (ssize_sum << 17) ^ N ^ (M << 32)` (u64) | identical u64 expression |
| `printf` CSV header + one data row | identical `println!` CSV header + one data row |
| monotonic phase timings `vector_us/string_us/total_us` | `std::time::Instant` phase timings in the same fields |

The PRNG call sequence is identical, so every push value, read index and string
chunk matches. `Instant` timings are printed in the same CSV shape, but the
benchmark figures below come from **external wall-clock timing**, not these
in-process phase fields.

**Determinism:** `BENCH_SEED = 0x9E3779B9` is fixed and never derived from time
or environment; no other randomness is used. Re-running with the same seed
always yields the same checksum.

## Checksum verification

- Independent Python re-implementation of the C+ PRNG and checksum formula:
  `vsum = 2148178203678760`, `ssize_sum = 50033432698`,
  checksum = **`5595356797869224`**.
- Rust binary: every one of the 1 warmup + 62 timed runs (two passes below)
  printed `5595356797869224` as the first CSV field.
- Live cross-check: `build/c+ build --release --legacy-c ... bench_modern.cp`
  produced the same CSV checksum `5595356797869224`. The `--release` (safe)
  build could not be produced at measurement time because of an unrelated,
  in-progress compile error in `c-plus-core`'s runtime
  (`cplus_runtime.c:137: use of undeclared identifier 'CPLUS_EXIT_ARITHMETIC'`);
  no safe-mode timing is claimed here, and no result was fabricated for it.

## Toolchain and exact build command

- rustc **1.100.0** (`0ef1bc386059596dada8fb65b50425aab2ea637a`, built
  2026-11-09, host `aarch64-apple-darwin`, LLVM 23.1.5). `rustc --version --verbose`
  was used to record this.
- Host: macOS / Darwin 27.2.0, `arm64`, Apple M2.
- `/usr/bin/timeout` is available and was used for every run.
- Build succeeded with **no warnings**. The binary was written to a temp
  directory outside the repository, so no file other than the two owned files
  was created in the repo.

```sh
# from the repo root; <out> below was the concrete temp directory
OUT=/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/bench_modern_rust_out
mkdir -p "$OUT"
rustc -O benchmarks/safety_compare/bench_modern.rs -o "$OUT/bench_modern"
```

`-O` means rustc's `-C opt-level=2` with default release-profile settings; no
other codegen flags were passed. Substitute any writable `<out>` to reproduce.

## Exact measurement method

One warmup run was discarded, then **31 timed runs**, each invoked as
`/usr/bin/timeout 60 <out>/bench_modern` plus a Python
`subprocess.run(..., timeout=60)` guard. Wall time used `time.perf_counter()`
around that wrapper, so figures include the small `timeout` fork/exec overhead.
p95 is linearly interpolated on sorted samples at rank `0.95*(n-1) = 28.50`.

```sh
python3 - <<'PY'
import subprocess, time, statistics
BIN = '<out>/bench_modern'
def run():
    t = time.perf_counter()
    r = subprocess.run(['/usr/bin/timeout','60',BIN],
                       capture_output=True, text=True, check=True, timeout=60)
    return time.perf_counter()-t, r.stdout.strip()
run()  # 1 warmup, discarded
samples, checksums = [], []
for _ in range(31):
    dt, out = run()
    samples.append(dt)
    checksums.append(out.splitlines()[-1].split(',')[0])
s = sorted(samples)
k = 0.95*(len(s)-1); lo = int(k); hi = min(lo+1, len(s)-1)
p95 = s[lo] + (s[hi]-s[lo])*(k-lo)
print('runs', len(samples), 'checksums', sorted(set(checksums)))
print('min', min(samples), 'median', statistics.median(samples),
      'max', max(samples), 'p95', p95)
PY
```

## Measured results — authoritative batch (31 timed runs, 1 warmup)

| Metric | Value |
|---|---:|
| median | **0.034649 s** |
| min | 0.023680 s |
| max | 0.085126 s |
| p95 (linear interpolation) | 0.055713 s |
| mean | 0.037402 s |
| checksum (warmup + all 31 runs) | `5595356797869224` |

Every run completed far below the 60 s timeout; none was killed or retried.
Raw samples (seconds, in run order): 0.034818, 0.031442, 0.034817, 0.085126,
0.029914, 0.030407, 0.027726, 0.023680, 0.029613, 0.029595, 0.026198,
0.041693, 0.054952, 0.034503, 0.049128, 0.052947, 0.056474, 0.044892,
0.033582, 0.028773, 0.029913, 0.037209, 0.037275, 0.031507, 0.029848,
0.035835, 0.034649, 0.035335, 0.039520, 0.030948, 0.037149.

Variance context: a second independent 1-warmup + 31-run pass on the same binary
gave min 0.023573 s, median 0.031618 s, max 0.050053 s, p95 0.044716 s, with the
same checksum. The spread between and within passes is shared-host noise (this
is an allocation- and PRNG-heavy workload on a busy machine); the first batch
above is the reported measurement. Do not read the sub-millisecond differences
between passes or between languages as meaningful on this host.

## Safety semantics — explicit note (do not overread this benchmark)

- **Bounds checks are retained in Rust release builds.** `-O` / the default
  release profile does **not** turn off bounds checking: the `v[idx]` indexing
  keeps its bounds-check semantics in MIR, and an out-of-bounds index has
  defined panic behaviour rather than undefined behaviour. LLVM may eliminate
  an *individual* check when it can prove the index in range (`idx` here comes
  from `% N`, so it is provably in range for the indexed vector), so the emitted
  machine code is not guaranteed to contain a trap on every access. This
  benchmark did **not** disassemble the binary to count residual checks.
- **Overflow checks are usually off in release.** The default release profile
  sets `overflow-checks = false`, so integer overflow wraps in release builds
  (debug builds panic). `-O` does not enable `-C overflow-checks=on`. The xorshift
  shifts/XORs and the `i64` accumulators are chosen so no overflow occurs here,
  so the checksum is unaffected, but this is a real semantic difference from
  debug mode.
- **This benchmark proves no safety property.** Identical checksums show the
  Rust and C+ programs computed the same integer result for one deterministic
  seed. Timing and checksum equality are **not** evidence of memory safety, of
  the presence/absence of bounds checks in machine code, or of "Rust is proven
  safe". Rust's safety argument comes from its type system and borrow checker at
  compile time, not from this timing harness.
- `black_box` on the printed checksum only prevents the result from being
  optimised away; it does not turn the benchmark into a correctness/safety test.
- Rust `Vec`/`String` are *different standard-library implementations* from C+
  `stdlib/vector.hp` / `stdlib/string.hp`. These numbers are a cross-language
  sanity check, not an apples-to-apples measurement of the C+ stdlib, and not a
  general performance claim about any language.
