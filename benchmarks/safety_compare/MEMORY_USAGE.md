# Peak RSS comparison: C+ and Rust safety-compare binaries (measured 2026-11-23)

Scope: **observed peak resident set size only.** This document does **not** compare
or infer safety guarantees — only what the OS actually reported for each process.
Plain C, Goose, and Fil-C are out of scope for this task and were not measured here
(Fil-C also has no installed toolchain). See [`RESULTS.md`](RESULTS.md) for the
speed numbers and workload definitions; the workloads here are the same ones.

> **Status and fairness note.** These RSS figures were measured from the
> already-built binaries listed below on 2026-11-23. The later reproducible
> driver run recorded in `results/table.txt` reports every `cplus_safe` row as
> **BLOCKED** (`0/7`, numeric columns `-`) by the `CPLUS_EXIT_ARITHMETIC`
> toolchain mismatch (`METHODOLOGY.md` §7.4), so these RSS values are earlier
> observations, not output of that driver; no safe-mode result is claimed here
> for the final driver. The `cplus_legacy` binaries are `--release --legacy-c`
> (legacy C, **not** memory safe), **not** the fair C+ speed configuration
> `--release --strict --sanitize=none`. RSS is not a speed metric, but do not
> present these variants as a fair C+ vs Rust speed comparison: a C+ `-O0`
> build against Rust `-O` is likewise not apples-to-apples (see
> `ALLOC_COMPARE_NOTES.md` and `METHODOLOGY.md` §1.3).

## Measurement tool

`/usr/bin/time -l <binary>` (macOS/BSD `time`). It is available on this host and
was used directly — no fallback tool was needed. The metric taken is the line

```
        NNN  maximum resident set size
```

which macOS reports in **bytes**. This is the process high-water RSS as sampled by
the kernel. The separate `peak memory footprint` line printed by `time -l` was
**not** used. Observed RSS values are exact multiples of 16384 bytes (16 KiB
pages on Apple silicon), so the effective resolution is one page.

Sanity check that the tool works and gives a byte value:

```sh
/usr/bin/time -l true 2>&1 | head -1
#        901120  maximum resident set size
```

## Environment

- Host: `Darwin ... 27.2.0 ... RELEASE_ARM64_T8112 arm64`; `Apple M2`; macOS 27.2.1 (26B131).
- `rustc 1.100.0 (0ef1bc386 2026-11-09)`.
- Backend C compiler for the C+ variants: `/usr/bin/clang` = Apple clang 16.0.0 (`clang-1600.0.26.6`).
- Binaries: the already-built files in `build/bench_safety/`, built exactly as documented
  in `RESULTS.md` (C+ at `--release`; Rust at `-O`).
- No rebuild was performed; only the existing binaries were measured.

## Protocol

- **Workloads (matched across variants):** `loop`, `nested`, `modern`.
- **Variants:** `cplus_safe`, `cplus_legacy`, `rust` (C+ safe = `--release` with
  UBSan instrumentation; C+ legacy = `--release --legacy-c`, no sanitizers).
- **Warmup:** 1 run per binary, discarded.
- **Measured runs:** 7 runs per binary, **interleaved round-robin** across all
  9 binary/workload pairs so they share the same CPU-frequency window.
- **Statistic:** median of the 7 per-run RSS values; min/max also recorded.
- stdout (the checksum) was captured and checked; stderr held the `time -l` report.
- A second confirmation pass (1 warmup + 5 interleaved runs) was run in a fresh
  process for repeatability.

Exact command used for the primary pass (run from the repo root):

```sh
python3 - <<'PY'
import subprocess, re, statistics, os
out='build/bench_safety'
workloads=['loop','nested','modern']
variants=['cplus_safe','cplus_legacy','rust']
bins={(v,w):f'{out}/{v}_{w}' for w in workloads for v in variants}
for k,p in bins.items():
    assert os.path.isfile(p) and os.access(p,os.X_OK), (k,p)

NRUNS=7
def measure(binpath):
    # /usr/bin/time -l <bin>; stdout=checksum, stderr=time report
    r=subprocess.run(['/usr/bin/time','-l',binpath],capture_output=True,text=True,timeout=300)
    assert r.returncode==0,(binpath,r.returncode,r.stderr[-500:])
    m=re.search(r'(\d+)\s+maximum resident set size',r.stderr)
    assert m, r.stderr
    return int(m.group(1)), r.stdout.strip()

for k,p in bins.items():
    measure(p)                      # warmup, discarded

samples={k:[] for k in bins}; checksums={}
for rnd in range(NRUNS):
    for k,p in bins.items():
        rss,chk=measure(p)
        samples[k].append(rss); checksums[k]=chk

for w in workloads:
    for v in variants:
        s=samples[(v,w)]
        print(f'{w} {v} median={int(statistics.median(s))} min={min(s)} max={max(s)} chk={checksums[(v,w)]}')
PY
```

Equivalent single-run form for any one binary:

```sh
/usr/bin/time -l build/bench_safety/cplus_safe_modern >/dev/null
```

## Workloads and checksums (matched)

| Workload | Work done | Checksum printed by all three variants |
|---|---|---:|
| `loop` | 2e8 integer sum | `19999999900000000` |
| `nested` | 4000² nested sum | `16000000` |
| `modern` | 2e6 `vector::push_back` + `at` reads, 2e5 `string::append` + `size` reads | `2200000200000` |

All three variants printed the identical checksum for each workload, so the
compared processes did the same work.

## Results — median peak RSS (primary pass, 7 interleaved runs after 1 warmup)

| Workload | Variant | Median RSS (bytes) | Median RSS (MiB) | min (bytes) | max (bytes) |
|---|---|---:|---:|---:|---:|
| loop | C+ safe (`--release`, UBSan) | 3,719,168 | 3.55 | 3,719,168 | 3,719,168 |
| loop | C+ legacy (`--release --legacy-c`) | 1,671,168 | 1.59 | 1,671,168 | 1,687,552 |
| loop | Rust (`-O`) | 1,851,392 | 1.77 | 1,851,392 | 1,851,392 |
| nested | C+ safe (`--release`, UBSan) | 3,719,168 | 3.55 | 3,719,168 | 3,735,552 |
| nested | C+ legacy (`--release --legacy-c`) | 1,671,168 | 1.59 | 1,654,784 | 1,671,168 |
| nested | Rust (`-O`) | 1,851,392 | 1.77 | 1,851,392 | 1,851,392 |
| modern | C+ safe (`--release`, UBSan) | 21,348,352 | 20.36 | 20,250,624 | 23,396,352 |
| modern | C+ legacy (`--release --legacy-c`) | 18,235,392 | 17.39 | 18,219,008 | 19,808,256 |
| modern | Rust (`-O`) | 14,188,544 | 13.53 | 14,188,544 | 14,188,544 |

1 MiB = 1,048,576 bytes. All values are reported by `time -l` in bytes; the MiB
column is a conversion for readability only.

Raw per-run samples (bytes, runs 1–7):

```
cplus_safe_loop:     [3719168, 3719168, 3719168, 3719168, 3719168, 3719168, 3719168]
cplus_legacy_loop:   [1687552, 1671168, 1671168, 1687552, 1687552, 1671168, 1671168]
rust_loop:           [1851392, 1851392, 1851392, 1851392, 1851392, 1851392, 1851392]
cplus_safe_nested:   [3719168, 3719168, 3735552, 3719168, 3719168, 3719168, 3719168]
cplus_legacy_nested: [1654784, 1671168, 1671168, 1671168, 1671168, 1671168, 1671168]
rust_nested:         [1851392, 1851392, 1851392, 1851392, 1851392, 1851392, 1851392]
cplus_safe_modern:   [20267008, 21856256, 21348352, 21381120, 20250624, 21086208, 23396352]
cplus_legacy_modern: [18219008, 18219008, 18251776, 19808256, 18235392, 18235392, 18235392]
rust_modern:         [14188544, 14188544, 14188544, 14188544, 14188544, 14188544, 14188544]
```

### Observed ratios (of the measured RSS only)

| Workload | C+ safe / C+ legacy | Rust / C+ legacy |
|---|---:|---:|
| loop | 2.23× | 1.11× |
| nested | 2.23× | 1.11× |
| modern | 1.17× | 0.78× |

These are ratios of observed byte values on this host, nothing more.

## Repeatability (second pass: 1 warmup + 5 interleaved runs)

| Workload | Variant | Median RSS (bytes) | min | max |
|---|---|---:|---:|---:|
| loop | C+ safe | 3,719,168 | 3,719,168 | 3,719,168 |
| loop | C+ legacy | 1,671,168 | 1,671,168 | 1,671,168 |
| loop | Rust | 1,851,392 | 1,851,392 | 1,867,776 |
| nested | C+ safe | 3,719,168 | 3,719,168 | 3,719,168 |
| nested | C+ legacy | 1,671,168 | 1,671,168 | 1,687,552 |
| nested | Rust | 1,851,392 | 1,835,008 | 1,851,392 |
| modern | C+ safe | 20,267,008 | 20,267,008 | 20,283,392 |
| modern | C+ legacy | 18,235,392 | 18,219,008 | 18,235,392 |
| modern | Rust | 14,188,544 | 14,188,544 | 15,663,104 |

`loop`/`nested` and `modern` C+ legacy / Rust medians reproduced to within one
page. `modern` C+ safe was the one noticeably variable case: its median was
21,348,352 bytes in the primary pass and 20,267,008 bytes in the confirmation
pass (range 20,250,624–23,396,352 bytes overall, about 19.3–22.3 MiB). Treat its
~20–21 MiB figure as approximate; the headline table uses the primary 7-run pass.

## Honest interpretation

- **`loop` and `nested` are tiny processes.** Their RSS (1.6–3.6 MiB) is dominated
  by process/runtime baseline, not by the arithmetic, which the compilers fold
  into a closed form (see `RESULTS.md`). The C+ safe numbers are ~2.2× the C+
  legacy numbers because the safe build links UBSan runtime instrumentation;
  Rust sits between them. This is baseline overhead, not "memory used by the
  workload" and not a statement about language efficiency.
- **`modern` is the only workload that actually allocates.** The measured RSS
  here reflects real container growth: C+ uses the checked `stdlib_alloc` path
  through `vector.hp`/`string.hp`, Rust uses `Vec`/`String` with its own growth
  strategy, and the C+ safe binary additionally carries sanitizer runtime state.
  These are *different standard-library implementations*, so the numbers are
  observations of these particular binaries, not a ranking of the languages.
- **Binary size ≠ RSS.** The Rust binaries are ~472 KB on disk versus ~50–58 KB
  for the C+ binaries, yet the Rust processes have the smallest resident set of
  the three on every workload. File size and peak RSS measure different things.
- **Scope:** only `maximum resident set size` from `/usr/bin/time -l` is compared.
  No safety guarantee, correctness property, or "memory safety" claim follows
  from these numbers. Equal or lower RSS never implies that C+ and Rust provide
  equivalent guarantees: C+ relies on runtime/API-boundary checks plus
  compile-time diagnostics, whereas Rust relies on compile-time ownership and
  borrow checking plus checked returns. Fil-C was not measured (no toolchain
  installed); plain C and Goose were not measured because this task covered C+
  and Rust only.
- One machine, three synthetic workloads, two measurement passes. Not a general
  memory-footprint claim about C+, Rust, or any standard library.
