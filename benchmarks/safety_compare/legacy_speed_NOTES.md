# Safe C+ (no sanitizer) vs legacy C — loop / nested workloads

Measured 2026-11-23. Scope is deliberately narrow: the two existing integer
workloads `benchmarks/safety_compare/loop.c` and `nested.c`, compiled two ways,
plus the plain-C twin added as `benchmarks/safety_compare/legacy_speed.c`.
No claim is made that any language is faster or safer than another.

## Question this answers

The earlier `RESULTS.md` measured C+ safe at `--release` **with UBSan on**
(`-fsanitize=undefined,bounds,null,alignment`) and reported ~29.85× / ~3.90×
overhead vs plain C on these two workloads. That table itself warns the numbers
are sanitizer-instrumentation effects. This run repeats the comparison with
**`--sanitize=none`** so only C+'s language-level safe lowering is measured, not
UBSan instrumentation.

## Why a C twin was needed

`loop.c` / `nested.c` are themselves plain C. Compiling them with
`c+ build --strict --sanitize=none --release` emits C that is byte-identical to
the source apart from a `#line` marker:

```sh
PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin \
  build/c+ build --strict --sanitize=none --release \
    --emit-c /tmp/loop.emitted.c benchmarks/safety_compare/loop.c -o /tmp/loop_bin
diff benchmarks/safety_compare/loop.c /tmp/loop.emitted.c
# only difference: leading '#line 1 "..."' directive and no trailing newline
```

So for these pure-arithmetic loops the safe mode inserts **no** bounds/null/
ownership checks; the C+ build adds no instrumentation to the hot code. The
two builds of the same `.c` file are therefore a valid checksum-identical pair
but a weak "safe vs legacy" contrast. `legacy_speed.c` is the required clean,
self-contained plain-C twin of both workloads (same constants, same `volatile`
sink, identical checksums), compiled with `/usr/bin/clang -O2`, so the
legacy-C reference is explicit and reproducible in one command.

## Exact build commands (from the repo root)

Binaries were written outside the repo to
`$TD=/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/igi_speed`
so no other repository file was modified.

```sh
TD=/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/igi_speed
mkdir -p "$TD"

# legacy C (-O2)
/usr/bin/clang -O2 benchmarks/safety_compare/loop.c    -o "$TD/c_loop"
/usr/bin/clang -O2 benchmarks/safety_compare/nested.c  -o "$TD/c_nested"
/usr/bin/clang -O2 benchmarks/safety_compare/legacy_speed.c -o "$TD/c_twin"

# safe C+ (STRICT, no sanitizers), matched -O2 via --release
PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin \
  build/c+ build --strict --sanitize=none --release \
    benchmarks/safety_compare/loop.c   -o "$TD/cp_loop"
PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin \
  build/c+ build --strict --sanitize=none --release \
    benchmarks/safety_compare/nested.c -o "$TD/cp_nested"
```

`--release` is required for a matched comparison: `c+ build --strict
--sanitize=none` without it defaults to `-O0 -g` (verified with `--verbose`),
which would conflate optimisation level with safety mode. `--strict` is the
fail-closed mode; `--sanitize=none` is the documented alias of `--no-sanitize`
and disables all backend sanitizers. Both C+ builds used backend
`/usr/bin/clang` (Apple clang 16), the same compiler as the legacy builds.

## Checksum verification (all identical)

| Binary | loop | nested |
|---|---|---|
| `clang -O2` on `loop.c` / `nested.c` | 19999999900000000 | 16000000 |
| `c+ --strict --sanitize=none --release` on `loop.c` / `nested.c` | 19999999900000000 | 16000000 |
| `clang -O2` on `legacy_speed.c` | 19999999900000000 | 16000000 |

Semantics match across all three variants for both workloads.

## Timing method

Python `subprocess`/`time.perf_counter` harness, variants interleaved
round-robin each round so they share CPU-frequency windows. One warmup run per
variant (discarded), then **11 timed runs** (pass 1) and an independent **31
timed runs** (pass 2, the primary result). Statistics: `statistics.median`,
`min`, `max`, and p95 by linear interpolation between closest ranks (type 7:
`h=(n-1)*0.95`, `p95 = s[lo] + (h-lo)*(s[hi]-s[lo])` on the sorted sample).

```sh
python3 - <<'PY'
import subprocess, time, statistics
TD = "/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/igi_speed"
RUNS = 31
def p95(data):
    s = sorted(data); h = (len(s)-1)*0.95
    lo = int(h//1); hi = min(lo+1, len(s)-1)
    return s[lo] + (h-lo)*(s[hi]-s[lo])
workloads = {
 "loop":   [("legacy clang -O2 loop.c", [f"{TD}/c_loop"]),
            ("safeC+ strict none release loop.c", [f"{TD}/cp_loop"]),
            ("legacy twin legacy_speed.c loop", [f"{TD}/c_twin","loop"])],
 "nested": [("legacy clang -O2 nested.c", [f"{TD}/c_nested"]),
            ("safeC+ strict none release nested.c", [f"{TD}/cp_nested"]),
            ("legacy twin legacy_speed.c nested", [f"{TD}/c_twin","nested"])],
}
for w, vs in workloads.items():
    for _, cmd in vs:  # warmup
        subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True, timeout=300)
    samp = {l: [] for l, _ in vs}
    for _ in range(RUNS):
        for l, cmd in vs:
            t = time.perf_counter()
            subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True, timeout=300)
            samp[l].append(time.perf_counter() - t)
    for l, _ in vs:
        s = samp[l]
        print(f"{w:7s} {l:38s} median={statistics.median(s):.6f}s "
              f"min={min(s):.6f}s max={max(s):.6f}s p95={p95(s):.6f}s")
PY
```

## Results — pass 2 (primary, n=31 per variant, 1 warmup)

| Workload | Variant | median | min | max | p95 | checksum |
|---|---|---:|---:|---:|---:|---:|
| loop | legacy `clang -O2 loop.c` | 0.004943s | 0.002117s | 0.007673s | 0.007024s | 19999999900000000 |
| loop | safe C+ `--strict --sanitize=none --release loop.c` | 0.005044s | 0.002187s | 0.016466s | 0.007198s | 19999999900000000 |
| loop | legacy twin `clang -O2 legacy_speed.c loop` | 0.005096s | 0.002149s | 0.010464s | 0.005977s | 19999999900000000 |
| nested | legacy `clang -O2 nested.c` | 0.004811s | 0.002175s | 0.005560s | 0.005301s | 16000000 |
| nested | safe C+ `--strict --sanitize=none --release nested.c` | 0.004996s | 0.002195s | 0.007272s | 0.005800s | 16000000 |
| nested | legacy twin `clang -O2 legacy_speed.c nested` | 0.004841s | 0.002135s | 0.007009s | 0.005781s | 16000000 |

Median ratios vs legacy `clang -O2` on the same workload:

- loop: safe C+ **1.020×**, legacy twin 1.031×
- nested: safe C+ **1.039×**, legacy twin 1.006×

## Results — pass 1 (cross-check, n=11 per variant, 1 warmup)

| Workload | Variant | median | min | max | p95 |
|---|---|---:|---:|---:|---:|
| loop | legacy `clang -O2 loop.c` | 0.003300s | 0.002212s | 0.005462s | 0.005257s |
| loop | safe C+ `--strict --sanitize=none --release loop.c` | 0.003064s | 0.002279s | 0.007986s | 0.007729s |
| loop | legacy twin `clang -O2 legacy_speed.c loop` | 0.002900s | 0.002268s | 0.005046s | 0.004788s |
| nested | legacy `clang -O2 nested.c` | 0.003049s | 0.002311s | 0.004888s | 0.004809s |
| nested | safe C+ `--strict --sanitize=none --release nested.c` | 0.003468s | 0.002335s | 0.006338s | 0.005597s |
| nested | legacy twin `clang -O2 legacy_speed.c nested` | 0.003877s | 0.002245s | 0.005156s | 0.004806s |

Median ratios vs legacy `clang -O2`: loop safe C+ 0.928×, nested safe C+ 1.137×.

## Honest interpretation

- **No measurable safe-mode overhead here.** With sanitizers disabled, the C+
  safe build of these workloads ran at ~1.0× the legacy build in both passes
  (0.93×–1.14× across passes). For pure integer loops with no memory access,
  strict safe mode emits the same arithmetic as plain C, so there is nothing to
  instrument. This is consistent with the earlier `RESULTS.md` note that its
  ~30×/3.9× figures came from UBSan instrumentation, not from C+ safe lowering.
- **These timings are not "loop execution time".** At `-O2` both compilers fold
  the arithmetic into a closed form (the `volatile sink` only forces the store).
  The ~2–5 ms measurements are dominated by process spawn/teardown and `printf`;
  identical checksums confirm semantics, not that 2×10⁸ iterations were run.
  Ratios near 1.0× should be read as "no detectable overhead", not as evidence
  that one compiler generates faster code.
- **Variance is material.** Host noise moved the legacy median from ~3.0 ms
  (pass 1) to ~4.9 ms (pass 2) and produced per-run outliers up to 16.5 ms.
  The pass-2 n=31 numbers are the more controlled estimate; all ratios sit
  inside that noise band. Do not treat small ratio differences as significant.
- **C twin is a reference, not a different implementation.** It reproduces the
  same three arithmetic lines and checksums as `loop.c`/`nested.c`; it exists so
  the legacy-C side is a single explicit source file, not because the C+ build
  of the `.c` files failed to compile (it did not).
- **Not measured here:** the UBSan-instrumented C+ safe build (see `RESULTS.md`),
  the modern-stdlib workload, Rust, the inspiration language, and Fil-C. No result is invented for
  any of them. This is one synthetic workload pair on one machine and is not a
  general performance, safety, or language-superiority claim.

## Environment

- Host: macOS arm64, Darwin 27.2.0 (`RELEASE_ARM64_T8112`), `sysctl` reports
  "Apple M2"; measured 2026-11-23 IST.
- Legacy C and C+ backend: Apple clang 16.0.0 (`clang-1600.0.26.6`),
  `/usr/bin/clang`.
- C+ driver `C+ 0.1.0`, Backend: host Clang/LLVM; safe mode flags at `--release`
  include `-O2 -std=gnu17 -DCPLUS_SAFE=1` and no `-fsanitize=*` with
  `--sanitize=none`; links `libcplus_runtime.a`.
- All builds succeeded with no warnings; no compiler, runtime, or benchmark
  harness source was modified. No other repository file was created by this
  run (binaries and timing JSON live in the external temp dir above).
