# ABI preservation and performance measurement — container-safety Phase 6

- **Branch / date:** `agent/modern-stdlib`, 2026-11-23.
- **File ownership for this task:** `docs/stdlib/container_safety/ABI_PERFORMANCE.md`
  only. No source, header, test, build file, or git index entry was created or
  modified by this author. All suites below were **executed** during this task;
  their outputs are reproduced verbatim.

Labels:

- **[MEASURED HERE]** — command run and output observed during this task.
- **[QUOTED]** — number taken from a checked-in result file, not re-run here.
- **[UNMEASURED]** — no number exists in the checkout; none is invented.

No performance figure below was produced by this author from a benchmark run.
The ABI results are from fresh runs; the performance table is an exact quote
from `benchmarks/safety_compare/RESULTS.md`.

---

## 1. ABI suites — exact results and toolchain

### 1.1 `run_abi_compat.sh` — PASS **[MEASURED HERE]**

Command (from the repo root):

```sh
sh tests/abi/run_abi_compat.sh
```

Final line and exit code:

```
abi_compat: 69 passed, 0 failed
EXIT_RC=0
```

Per-probe summary lines (from `build/abi/compat/*.out`):

| Probe | Recorded line |
|---|---|
| `pointers.out` | `abi_compat_pointers: 20/20 pointer ABI checks match` |
| `layouts.out` | `abi_compat_layouts: 80/80 layouts match` |
| `c_driver.out` | `abi_compat_c_driver: 13/13 C->C+ boundary checks match` |
| `cplus_caller.out` | `abi_compat_cplus_caller: 10/10 C+->C boundary checks match` |

`build/abi/compat/results.log` ends with
`RESULT: abi_compat: 69 passed, 0 failed`. This suite pins its own toolchain:

```sh
export PATH=/usr/bin:/bin
export CPLUS_CLANG=/usr/bin/clang
CC=/usr/bin/clang
```

It builds both real static libraries, checks exported symbol names against
three allow-lists, asserts no ` T __Z` C++ mangling, exercises real C↔C+ calls
with `abi_compat_record` by value and by pointer in both directions, and diffs
ordinary-C layouts with and without the C+ public headers force-included.

### 1.2 `run_abi_layouts.sh` — PASS only with the compiler pinned **[MEASURED HERE]**

The script does not pin its C compiler. With the ambient environment it fails
before reaching any ABI check:

```sh
sh tests/abi/run_abi_layouts.sh
# clang: warning: no such sysroot directory: '/Library/Developer/CommandLineTools/SDKs/MacOSX27.sdk'
# clang: warning: using sysroot for 'macOS 27' but targeting 'arm64-apple-macosx27.0.0'
# libc/log/log.c:27:10: fatal error: 'errno.h' file not found
# EXIT_RC=1
```

**Cause (environmental, not an ABI failure):** the ambient `PATH` resolves
`clang` to `/opt/homebrew/opt/llvm/bin/clang` (Homebrew clang 23.1.6), whose
sysroot points at the nonexistent `MacOSX27.sdk`. The script calls
`sh tests/libc/build_libc.sh`, which uses `CC=${CPLUS_CLANG:-clang}`, so that
broken compiler builds the libc archive. The compat suite avoids this by
exporting `CPLUS_CLANG=/usr/bin/clang`. Pinning the same compiler makes the
layout suite pass:

```sh
CPLUS_CLANG=/usr/bin/clang sh tests/abi/run_abi_layouts.sh
# clang: warning: build/libc/libcplus_hardened.a: 'linker' input unused [-Wunused-command-line-argument]
# clang: warning: build/safe/libcplus_safecrt.a: 'linker' input unused [-Wunused-command-line-argument]
abi_layouts: 70/70 layouts match
EXIT_RC=0
```

The two `linker input unused` warnings are benign (the archives are on the link
line, not consumed during compile). `build/abi/host.out` and
`build/abi/cplus.out` are byte-identical (72 lines each; `diff -q` reported
`IDENTICAL`), and both start with:

```
abi_layouts: baseline sizeof(void*)=8 sizeof(size_t)=8 sizeof(unsigned long)=8
```

`N = 70` = 36 size/alignment + 21 field-offset + 13 enumerator-value
assertions over every public struct/enum in `libc/install-include` and
`stdlib/safe` (`tests/abi/abi_layouts.c`).

### 1.3 Compiler and architecture (exact, from this host)

| Item | Value |
|---|---|
| `uname -a` | `Darwin Haseebs-MacBook-Pro.local 27.2.0 Darwin Kernel Version 27.2.0 ... RELEASE_ARM64_T8112 arm64` |
| Arch / OS | `arm64`, Darwin 27.2.0 (macOS, Apple M3 Pro `T8112`) |
| `/usr/bin/clang --version` | `Apple clang version 16.0.0 (clang-1600.0.26.6)`, target `arm64-apple-darwin27.2.0` |
| `./build/c+ --version` | `C+ 0.1.0`; `Backend: host Clang/LLVM` |
| Ambient `clang` (the broken one) | `/opt/homebrew/opt/llvm/bin/clang`, `Homebrew clang version 23.1.6` |

All results are **LP64 arm64** only. No Windows x86 evidence is claimed here
(see §4).

---

## 2. Did this task change the ABI? — No.

**Explicit statement: there is no ABI change from this task.** No fix on this
branch altered a public struct layout. The check performed:

- `git diff` of every modified header on `agent/modern-stdlib`
  (`stdlib/collections.hp`, `stdlib/error.hp`, `stdlib/file.hp`, `stdlib/io.hp`,
  `stdlib/log.hp`, `stdlib/string.hp`, `tests/native/cplus_test_native.hp`)
  shows **only method-body / function-body logic changes**, with no data-member
  additions, removals, renames, type changes, or reordering. Examples of the
  actual changes:
  - `collections.hp`: ring/deque/queue index computation rewritten from
    `% space` to a one-subtraction conditional; no field touched.
  - `error.hp`: `stdlib_calloc` overflow check switched to
    `__builtin_mul_overflow`; a new `static inline stdlib_safe_div` helper added;
    `error_fail_cat` copies `message` with `strlen`+`memcpy`. All are function
    bodies; the public `error` struct and `cplus_alloc_meta` mirror are
    unchanged.
  - `file.hp`, `io.hp`, `log.hp`, `string.hp`: pointer reads/writes rewritten as
    `__builtin_memcpy` for the strict checker; no struct definition changed.
- `static inline` additions add **no exported ABI symbol** (confirmed by the
  `nm -gU` audit: 69/0).
- The two ABI probes confirm the observable outcome directly: `70/70 layouts
  match` (host and C+ backend byte-identical) and `80/80` public layout
  assertions in the compat suite. The baseline remains 8-byte pointers,
  `size_t`, and `unsigned long`; pointers stay thin (metadata is out-of-band,
  keyed by base address — no inline header, tag, or capability word).
- The mod-relevant public C headers under `libc/install-include` and
  `stdlib/safe` are unmodified by this task and are exactly the types the ABI
  probe checks.

If a future fix **does** change a struct layout, the ABI must be re-baselined;
that condition does not hold for any change present on this branch.

---

## 3. Performance measurement

### 3.1 Recorded safe-vs-legacy data **[QUOTED from `benchmarks/safety_compare/RESULTS.md`]**

I did not re-run these benchmarks. The source file records measurements dated
2026-11-22 and 2026-11-23 on macOS arm64 / Apple M3 Pro, Apple clang 16,
rustc 1.100.0, the inspiration language v2026.4; both C+ variants at `-O2` (`--release`). Safe mode
adds `-fsanitize=undefined,bounds,null,alignment`,
`-fno-sanitize-recover=all`, `-fno-omit-frame-pointer`; legacy is
`-std=gnu89 -DCPLUS_LEGACY_C=1` with no sanitizers.

| Workload | plain C `-O2` | C+ safe (median) | C+ legacy (median) | Rust `-O` | the inspiration language |
|---|---:|---:|---:|---:|---:|
| loop sum (2e8) | 0.0036s | 0.1075s | 0.0038s | 0.0042s | 0.0043s |
| nested (4000²) | 0.0023s | 0.0090s | 0.0023s | 0.0026s | 0.0024s |
| modern stdlib (7 interleaved runs) | — | 0.0140s (min 0.0134, max 0.0188) | 0.0061s (min 0.0052, max 0.0077) | 0.0060s | 0.0044s |

Recorded ratios vs plain C: safe 29.85x / 3.90x; legacy 1.05x / 1.02x;
Rust 1.18x / 1.14x; the inspiration language 1.19x / 1.06x. For the modern stdlib workload the
recorded ratio is safe **2.31×** vs C+ legacy (Rust 0.99×, the inspiration language 0.72×). All
variants printed checksum `2200000200000`.

The "modern" workload exercises `stdlib/string.hp` and `stdlib/vector.hp`:
2,000,000 × `push_back` + `at` reads and 200,000 × `append`. Its source files
are `benchmarks/safety_compare/modern.{cp,rs,scoped}`.

**Honest interpretation (quoted from `RESULTS.md`):** the fair comparison is
C+ safe vs C+ legacy — same driver, same `-O2`, same source; the only
difference is safe-mode UBSan instrumentation. The non-instrumented compilers
folded the integer loops into closed forms, so the 0.002–0.004s loop/nested
timings measure that optimisation, not loop execution. Rust/the inspiration language numbers are
cross-checks with different standard libraries, not language speed claims.
Variance is material on this host (safe/legacy ratio ranged ~1.8×–3.1× across
three non-interleaved passes). Fil-C was **not measured** (no toolchain
installed). These are one synthetic workload set on one machine, not a general
performance claim.

### 3.2 Isolated metadata overhead — NOT measured **[UNMEASURED]**

**No isolated overhead number exists for the out-of-band metadata (side table,
generation counters, allocation registry, recently-free cache) on this branch.**
No benchmark in the checkout toggles those mechanisms independently of
safe-mode sanitizers. The 2.31×/29.85×/3.90× figures above are whole-mode
safe-vs-legacy numbers; `RESULTS.md` explicitly attributes them to UBSan
instrumentation, **not** to a separately measured metadata cost. No number is
invented here, and none of the ratios above may be reported as "metadata
overhead."

### 3.3 Exact command to measure the missing isolated overhead (not executed)

No build flag disables the metadata, so a small experiment is required. Run
this from the repo root. It keeps one allocation alive and times only metadata
queries, using `--sanitize=none` so the comparison isolates metadata code from
UBSan; the second fixture is the same loop with the metadata calls replaced by
an equivalent amount of trivial work. Use the same 1-warmup / 7-interleaved-run
/ median discipline as `RESULTS.md`. The fixture writes into `build/` only
(generated artifacts), so it does not affect ownership of tracked files.

```sh
mkdir -p build/bench_meta
cat > build/bench_meta/meta_overhead.cp <<'CP'
#include <stdlib/error.hp>
#define N 2000000
int main(void) {
    void *p = stdlib_alloc(64);
    unsigned long long g;
    if (p == NULL) return 1;
    g = stdlib_alloc_generation(p);
    for (int i = 0; i < N; ++i) {
        (void)stdlib_alloc_size(p);
        (void)stdlib_alloc_generation(p);
        (void)stdlib_alloc_check_generation(p, g);
    }
    stdlib_release(p);
    return 0;
}
CP
cat > build/bench_meta/baseline.cp <<'CP'
#define N 2000000
volatile unsigned long sink;
int main(void) {
    unsigned long g = 1;
    for (int i = 0; i < N; ++i) {
        sink = (unsigned long)i;   /* same trip count, no metadata queries */
        g = g + 1;
    }
    if (g == 0) return 1;
    return 0;
}
CP
PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin \
  build/c+ build --sanitize=none -O2 build/bench_meta/meta_overhead.cp \
  -o build/bench_meta/meta_overhead
PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin \
  build/c+ build --sanitize=none -O2 build/bench_meta/baseline.cp \
  -o build/bench_meta/baseline
python3 - <<'PY'
import subprocess, time, statistics
bins = {'meta': 'build/bench_meta/meta_overhead', 'baseline': 'build/bench_meta/baseline'}
for v, b in bins.items():
    subprocess.run([b], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
samples = {v: [] for v in bins}
for rnd in range(7):
    for v, b in bins.items():
        t = time.perf_counter()
        subprocess.run([b], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
        samples[v].append(time.perf_counter() - t)
for v in bins:
    s = samples[v]
    print(f'{v:8s} {statistics.median(s):8.4f}s  min={min(s):.4f} max={max(s):.4f}')
PY
```

Note: the metadata API names/signatures above are verified against
`stdlib/error.hp` (`stdlib_alloc` at :365, `stdlib_alloc_size` at :477,
`stdlib_alloc_generation` at :513, `stdlib_alloc_check_generation` at :524,
`stdlib_release` at :582). I did **not** execute this command, so it reports no
result. A single `stdlib_alloc` is kept alive so the queries hit a live slot;
the loop cost is then the metadata lookup/lock cost rather than allocation
growth. A follow-up that also times the alloc/free path itself would be needed
to separate insertion/removal cost from query cost.

To reproduce the **recorded** whole-mode numbers instead:

```sh
CPLUS_SCOPED=<path> sh benchmarks/safety_compare/run_bench.sh
```

---

## 4. Blockers, limitations, and honest scope

1. **Layout suite compiler pinning (resolved, environmental).**
   `run_abi_layouts.sh` does not set `CPLUS_CLANG`; on this host that lets
   Homebrew clang 23.1.6 with a broken `MacOSX27.sdk` sysroot build the libc
   archive and fail on `errno.h`. Pin `CPLUS_CLANG=/usr/bin/clang` as shown in
   §1.2. The compat suite already pins its toolchain and needs no workaround.
   No source change was made to fix this (outside this task's file ownership).
2. **No Windows x86 evidence.** All ABI results are Apple clang
   16 / arm64 (LP64). Windows x86 layout, packing, calling convention, exports,
   and ordinals are untested; no DLL-live success is claimed.
3. **Isolated metadata overhead is unmeasured** (§3.2). The exact experiment is
   given in §3.3 but was not run; every measured ratio in §3.1 is whole-mode.
4. **Benchmarks not re-run.** The §3.1 table is a faithful quote from
   `benchmarks/safety_compare/RESULTS.md`; I did not execute the benchmark
   scripts, and I added no new measured number.
5. **Not a memory-safety proof.** ABI agreement and contract-conformance passes
   do not prove memory safety or live-game behaviour.

---

## 5. Summary

| Item | Result | Status |
|---|---|---|
| `sh tests/abi/run_abi_compat.sh` | `abi_compat: 69 passed, 0 failed` (RC 0) | **[MEASURED HERE]** |
| `sh tests/abi/run_abi_layouts.sh` (ambient) | fails: broken sysroot / `errno.h` not found (RC 1) | **[MEASURED HERE]** — environmental |
| `CPLUS_CLANG=/usr/bin/clang sh tests/abi/run_abi_layouts.sh` | `abi_layouts: 70/70 layouts match` (RC 0); host/C+ outputs byte-identical | **[MEASURED HERE]** |
| Compiler / arch | Apple clang 16.0.0; `build/c+` 0.1.0; Darwin 27.2.0 arm64 (T8112) | **[MEASURED HERE]** |
| Struct layout changed by this task? | **No** — header diffs are function bodies only; public C headers unmodified; ABI probes pass | checked via `git diff` |
| Safe-vs-legacy whole-mode overhead | 0.1075s/0.0038s loop, 0.0090s/0.0023s nested, 0.0140s/0.0061s modern (2.31×) | **[QUOTED]** `RESULTS.md` |
| Isolated metadata overhead | none measured; command provided in §3.3 | **[UNMEASURED]** |

**Evidence inventory:** `tests/abi/run_abi_compat.sh`, `tests/abi/run_abi_layouts.sh`,
`build/abi/compat/results.log`, `build/abi/compat/{pointers,layouts,c_driver,cplus_caller}.out`,
`build/abi/host.out`, `build/abi/cplus.out`, `benchmarks/safety_compare/RESULTS.md`,
`stdlib/error.hp`, `docs/stdlib/abi-preserving-runtime-safety.md`.
