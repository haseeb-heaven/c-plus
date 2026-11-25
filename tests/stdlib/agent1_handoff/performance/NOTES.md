# P2 handoff — Performance comparison against Clang and Fil-C

Owner of this note: the handoff/documentation agent. This is a **documentation-only**
deliverable: it records existing evidence, defines a fair protocol, and lists
concrete blockers. It does **not** add benchmarks, rerun benchmarks, edit compiler
or runtime code, or claim that any language is faster or safer than another.

Task identity (from `docs/libc/AGENT1_PRIORITY_HANDOFF.md`):

- line 24: `| P2 | Performance vs Clang and Fil-C | benchmarks/safety_compare/RESULTS.md
  has C+/Rust/Goose medians; Fil-C toolchain absent | tests/stdlib/agent1_handoff/performance/ |`
- line 62: P2 definition of done — "performance is measured with identical
  workloads against Clang and (when available) Fil-C."

Target document: `tests/stdlib/agent1_handoff/performance/NOTES.md` (this file).

---

## 1. Evidence base — files inspected

| File | What it supplies |
| --- | --- |
| `benchmarks/safety_compare/RESULTS.md` | Two dated measurement reports (2026-11-22 integer workloads; 2026-11-23 modern stdlib workload) with medians, environment, commands, denominators and explicit caveats. |
| `benchmarks/safety_compare/run_bench.sh` | Exact build/timing commands for `loop` and `nested`; compiler pinning; the 5-run/1-warmup/median-of-4 denominator; the Fil-C "not measured" note. |
| `benchmarks/safety_compare/modern.cp`, `modern.rs`, `modern.goose` | Identical workload definitions, N/M constants and the shared checksum; basis of the safe-vs-legacy fairness argument. |
| `benchmarks/safety_compare/loop.c`, `nested.c` | Workload denominators (2e8 iterations; 4000² inner iterations). |
| `benchmarks/run_bench.sh`, `benchmarks/timeit.py` | The other, older 8-benchmark suite: Clang `-O2` reference build line, 5-run median discipline (timeit warms up once then times N runs). |
| `docs/LIMITATIONS.md` § "Performance measurements" | The only table with a dedicated `clang -O2` column (8 benchmarks) and its interpretation. |
| `docs/RELATED.md`, `docs/FOUNDATION.md`, `docs/UPSTREAM_AUDIT.md`, `docs/AUDIT.md`, `docs/libc/UPSTREAM_EVALUATION.md` | Why Fil-C was rejected as a foundation: full Clang fork + GC + custom ABI; no build exists here. |
| `docs/stdlib/upstream_study/fil_c.md` §8 | The concrete Fil-C platform/toolchain blockers (ELF/Linux, no 32-bit support, whole-program ABI, absent components). |
| `tools/build-win32-dll.sh` | **Absent** in this checkout; `tools/` contains only `verify.sh`. The Windows build path named in AGENTS.md / the handoff prompt is therefore not reproducible here. |

Non-benchmark environment verification performed for this note (tool/version checks
only; **no benchmark was executed**):

```text
$ which filc
filc not found
$ /usr/bin/clang --version | head -1
Apple clang version 16.0.0 (clang-1600.0.26.6)
$ rustc --version
rustc 1.100.0 (0ef1bc386 2026-11-09)
$ /Users/haseeb-mir/.local/bin/goose --version
goose v2026.4-57-g9412b80f (runtime 41e336e5d1ee)
$ ls tools
verify.sh
```

Pre-existing binaries in `build/bench_safety/` (timestamps 2026-11-22 and
2026-11-23) corroborate that the numbers below come from prior recorded runs,
not from any run performed while writing this note.

---

## 2. Recorded measurements (exact transcription, no new numbers)

### 2.1 Integer workloads — measured 2026-11-22 (`benchmarks/safety_compare/RESULTS.md` lines 1–34)

Command: `CPLUS_GOOSE=<path> sh benchmarks/safety_compare/run_bench.sh`

Host/toolchain as recorded: "macOS arm64, Apple clang 16, rustc 1.100.0, Goose
v2026.4, C+ driver at `-O2 --release`."

Denominator: "Each binary ran 5 times; the first run was discarded as warmup and
the median of the remaining 4 is reported. All four languages produced identical
checksums for both workloads." This matches `run_bench.sh` lines 36–42
(`for k in range(5)`, `if k>0: times.append(dt)`, `statistics.median(times)`).

Interpretation of the "plain C `-O2`" column: `run_bench.sh` line 11 sets
`CLANG=/usr/bin/clang` and line 17 builds with `"$CLANG" -O2 ...`. **Plain C -O2
in this table is therefore the Clang -O2 reference** for these two workloads.

| Workload | plain C / Clang `-O2` | C+ safe (`--release`, UBSan) | C+ legacy (`--release --legacy-c`) | Rust `-O` | Goose |
| --- | ---: | ---: | ---: | ---: | ---: |
| loop sum (2e8) | 0.0036s | 0.1075s | 0.0038s | 0.0042s | 0.0043s |
| nested (4000²) | 0.0023s | 0.0090s | 0.0023s | 0.0026s | 0.0024s |

Ratios vs plain C, as recorded: safe **29.85x / 3.90x**; legacy **1.05x / 1.02x**;
Rust **1.18x / 1.14x**; Goose **1.19x / 1.06x**.

Workload denominators from source: `loop.c` sums `i = 0 .. 200000000-1`
(2e8 iterations); `nested.c` runs 4000×4000 = 16,000,000 inner iterations
computing `(long long)i*j - (long long)j*i + 1`. RESULTS.md records only that
the checksums were *identical*, not their literal values; do not invent them.

Caveats stated in RESULTS.md (must travel with any reuse of these numbers):

- The fairest comparison is C+ safe vs C+ legacy — same driver, same
  `-O2 --release`, only the safe-mode UBSan instrumentation differs.
- "All non-instrumented compilers folded the arithmetic loops into closed-form
  results (identical checksums prove semantics, but the 0.002–0.004s timings are
  dominated by that optimisation, not by loop execution)." These numbers must
  **not** be read as "Rust/Goose are 30x faster than C+"; they are optimisation
  differences.
- `goose` is installed but outside the restricted benchmark `PATH`; its path must
  be supplied via `CPLUS_GOOSE`.

### 2.2 Modern stdlib workload — measured 2026-11-23 (`RESULTS.md` lines 37–154)

This workload is defined against the modern stdlib layer (`stdlib/string.hp`,
`stdlib/vector.hp`). It was measured with **standalone commands**, not
`run_bench.sh`, because that script only loops over `loop`/`nested` and has no
plain-C source for this workload (it is defined in terms of C+ stdlib types).

Workload (from `modern.cp`):

- `vector v`: `N = 2,000,000` × `push_back(i)`, then `N` × `at(i)` summed into `vsum`.
- `string s`: constructed as `"a"`, then `M = 200,000` × `append("bbbbbbbbbb")`
  with `size()` accumulated into `ssize_sum`.
- Printed checksum `vsum + ssize_sum` = **`2200000200000`** for every variant.
  Arithmetic check from the source constants: `vsum = N(N-1)/2 =
  1,999,999,000,000`; `ssize_sum = sum_{k=1..M}(1+10k) = 200,001,200,000`;
  total `2,200,000,200,000`. Rust uses `Vec<i32>::push`/indexing and
  `String::push_str`/`len()`; Goose uses `i32[>..]` and `u8[>..]` dynamic arrays.
  All binaries printed the identical checksum.

Exact build commands (verbatim from RESULTS.md):

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
  -o build/bench_safety/goose_modern
```

`$CPLUS_GOOSE` was `/Users/haseeb-mir/.local/bin/goose`. The restricted `PATH`
pins the C+ backend to `/usr/bin/clang` (Apple clang 16) so the two C+ variants
differ only in safety mode.

Denominator: one untimed warmup per binary, then **7 interleaved timed runs**
per binary, round-robin across variants, median of the 7. Verbatim harness:

```sh
python3 - <<'PY'
import subprocess,time,statistics
out='build/bench_safety'
variants=['cplus_safe','cplus_legacy','rust','goose']
bins={v:f'{out}/{v}_modern' for v in variants}
for v in variants: subprocess.run([bins[v]],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,check=True)
samples={v:[] for v in variants}
for rnd in range(7):
    for v in variants:
        t=time.perf_counter(); subprocess.run([bins[v]],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,check=True)
        samples[v].append(time.perf_counter()-t)
for v in variants:
    s=samples[v]; print(f'{v:14s} {statistics.median(s):8.4f}s  min={min(s):.4f} max={max(s):.4f}')
PY
```

Environment as recorded: macOS arm64, Darwin 27.2.0, Apple M3 Pro (`arm64`,
T8112); C+ `0.1.0` (Backend: host Clang/LLVM); safe = `Mode::Safe`
(`-fsanitize=undefined,bounds,null,alignment`, `-fno-sanitize-recover=all`,
`-fno-omit-frame-pointer`); legacy = `Mode::LegacyC` (`-std=gnu89`,
`-DCPLUS_LEGACY_C=1`, no sanitizers); backend C compiler for both C+ variants
and Goose = Apple clang 16.0.0 (`clang-1600.0.26.6`, `/usr/bin/clang` /
`/usr/bin/cc`); rustc 1.100.0 (`0ef1bc386 2026-11-09`, LLVM 23.1.5) at `-O`;
Goose `v2026.4-57-g9412b80f` (runtime `41e336e5d1ee`), default `-O2`. All builds
succeeded with no warnings.

| Variant | median (7 interleaved runs) | min | max | checksum |
| --- | ---: | ---: | ---: | ---: |
| C+ safe (`--release`, UBSan) | 0.0140s | 0.0134s | 0.0188s | 2200000200000 |
| C+ legacy (`--release --legacy-c`) | 0.0061s | 0.0052s | 0.0077s | 2200000200000 |
| Rust (`-O`) | 0.0060s | 0.0048s | 0.0105s | 2200000200000 |
| Goose (`-O2`) | 0.0044s | 0.0041s | 0.0090s | 2200000200000 |

Ratios of medians vs C+ legacy, as recorded: safe **2.31×**, Rust **0.99×**,
Goose **0.72×**.

Caveats stated in RESULTS.md: the fair comparison is safe vs legacy (same source,
same `-O2`, same stdlib; only instrumentation differs; applied to lowered
`string.hp`/`vector.hp` code too); this workload is allocation-heavy so loops
are *not* folded; Rust/Goose libraries are different implementations and their
numbers are cross-checks, **not** evidence of language superiority. Variance is
material: across three non-interleaved passes the safe/legacy ratio ranged
~1.8×–3.1× (legacy 0.0055–0.0095s, safe 0.0150–0.0172s).

### 2.3 Clang `-O2` column in `docs/LIMITATIONS.md` (older suite, lines 230–259)

This is the only table with a dedicated `clang -O2` column. It uses a different
suite and a different denominator, so it is **not** interchangeable with §2.1/§2.2.

Command: `./benchmarks/run_bench.sh` (`benchmarks/run_bench.sh`). Reference build
line (line 71):
`clang -std=gnu17 -O2 -pthread -I"$ROOT/runtime/include" -include cplus/cplus.h "$src" -o "$plain"`.
Denominator: 5 runs per binary, median reported via `benchmarks/timeit.py` (one
untimed warmup, then N timed runs; `statistics.median`). Host: "arm64 macOS,
Apple clang 16".

| benchmark | c+ safe | c+ legacy | clang -O2 | safe / clang | legacy / clang |
| --- | ---: | ---: | ---: | ---: | ---: |
| `alloc_churn` | 0.1877s | 0.1834s | 0.0532s | 3.53x | 3.45x |
| `bounds_sum` | 0.0327s | 0.0303s | 0.0312s | 1.05x | 0.97x |
| `function_call` | 0.1222s | 0.1176s | 0.1168s | 1.05x | 1.01x |
| `integer_compute` | 0.0958s | 0.0942s | 0.0940s | 1.02x | 1.00x |
| `method_dispatch` | 0.0189s | 0.0081s | 0.0051s | 3.75x | 1.60x |
| `parallel_scale` | 0.0157s | 0.0146s | 0.0141s | 1.11x | 1.03x |
| `pointer_chase` | 0.0352s | 0.0535s | 0.0528s | 0.67x | 1.01x |
| `raii_loop` | 0.0189s | 0.0105s | 0.0084s | 2.26x | 1.25x |

Interpretation recorded there: near-1.00x legacy ratios show lowering/destructor
insertion are essentially free after C optimisation; safe-mode cost is mostly
UBSan; `alloc_churn` shows ~3.4x managed-allocator overhead; timings vary between
runs and must be rerun, not treated as universal.

---

## 3. What was NOT measured (explicit, so nothing is fabricated)

1. **Fil-C** — not measured in either RESULTS.md report. Reason recorded in both
   the script and the report: no `filc` toolchain installed. Re-confirmed for
   this note (`which filc` → "filc not found"). No result is reported as zero,
   as a pass, or by proxy from another implementation.
2. **Windows** — no benchmark was run on Windows, on a Windows target, or through
   a 32-bit x86 DLL build. `tools/build-win32-dll.sh` does not exist in this
   checkout; `tools/` contains only `verify.sh`.
3. **Cross-OS / Linux** — all recorded numbers are from one macOS arm64 host
   (Darwin 27.2.0, Apple M3 Pro T8112). Linux CI jobs are noted as UNVERIFIED
   remotely in `docs/libc/AGENT1_PRIORITY_HANDOFF.md` line 23. No Linux number is
   recorded here.
4. **Clang -O2 on the modern allocation-heavy workload** — there is no plain-C
   source for `modern.cp`, because that workload is expressed in C+ stdlib types.
   The §2.2 table therefore has no Clang column; C+ legacy is its non-instrumented
   baseline, and Rust/Goose are cross-checks.
5. **Anything beyond the listed workloads** — the evidence covers two synthetic
   integer workloads, one synthetic allocation-heavy workload, and eight older
   microbenchmarks. None of these is a general performance claim about any
   language or about the C+ stdlib layer.

---

## 4. Fair comparison protocol (defined here, not yet executed)

Goals: identical workload and checksum across variants; isolate one variable at
a time; control drift; make every number reproducible from a recorded command.

### 4.1 Invariants

- **Identical workload**: one source workload per table row; each language
  variant performs the same N/M operations and prints the same integer checksum.
  A variant whose checksum differs is a build failure, not a timing result.
- **Compiler pinning**: C+ backend and any reference C compiler pinned by
  absolute path (`/usr/bin/clang`, Apple clang 16) via a restricted `PATH`:
  `PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin`. Do not rely on a
  Homebrew `clang` from `PATH` (see §6). Rust and Goose pinned by absolute path
  or `${CPLUS_RUSTC}` / `${CPLUS_GOOSE}` environment overrides.
- **Warmup**: exactly one untimed run per binary before sampling (discharges
  page faults and CPU frequency ramp-up; same discipline as `benchmarks/timeit.py`).
- **Interleaving**: timed runs are round-robin across all variants in each round,
  so variants share the same CPU-frequency/thermal windows. Do not time one
  variant's entire batch before the next.
- **Denominator**: at least 7 timed runs per variant; report median **and** min
  and max. Use `time.perf_counter()` around `subprocess.run`, redirect stdout to
  `DEVNULL`, and assert a zero exit status for every run.
- **Primary fair pair**: C+ safe vs C+ legacy — identical `.cp` source, identical
  `-O2 --release`, identical stdlib layer; only UBSan instrumentation differs.
  Use this pair for the headline overhead ratio.
- **Secondary cross-checks**: Clang `-O2` plain C (only where an equivalent
  plain-C source exists), Rust `-O`, Goose `-O2`, and Fil-C when a toolchain
  exists. These compare *different implementations*; report them, but do not
  interpret them as language rankings.
- **Reporting**: each table row must be accompanied by the exact build and
  timing commands, compiler versions, host, workload constants, checksum,
  run count and warmup rule, and median/min/max plus ratios with their stated
  denominator (e.g. "ratio of medians vs C+ legacy").

### 4.2 Variant matrix

| Variant | Build command (from repo root) | Role |
| --- | --- | --- |
| C+ safe | `PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin build/c+ build --release <src>.cp -o build/bench_safety/cplus_safe_<w>` | Primary instrumented baseline. |
| C+ legacy | `PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin build/c+ build --release --legacy-c <src>.cp -o build/bench_safety/cplus_legacy_<w>` | Primary non-instrumented baseline; same source/stdlib. |
| Clang `-O2` | `/usr/bin/clang -O2 <src>.c -o build/bench_safety/c_<w>` (safety_compare) **or** `clang -std=gnu17 -O2 -pthread -I runtime/include -include cplus/cplus.h <src>.c -o ...` (older suite) | Reference plain C; only where a C source exists. |
| Rust `-O` | `rustc -O <src>.rs -o build/bench_safety/rust_<w>` | Cross-check. |
| Goose | `"$CPLUS_GOOSE" build <src>.goose -o build/bench_safety/goose_<w>` | Cross-check. |
| Fil-C | **No command available** — toolchain absent (§6). Add only when a `filc` compiler + runtime exists, with its own documented command and a checksum-parity gate. | Missing; report "not measured". |

### 4.3 Timing harness template

Reuse the §2.2 interleaved harness verbatim, changing only the `variants` list,
the `_modern` binary suffix, and the round count if ≥7. For the `loop`/`nested`
workloads the existing script `benchmarks/safety_compare/run_bench.sh` may be
used as-is; note that its denominator is 5 runs / warmup discarded / median of 4
and it is **not** interleaved, so its ratios must be labelled with that
denominator and should not be merged into an interleaved table without re-running.

### 4.4 Reporting template (one row per workload)

```text
workload=<name>  date=<YYYY-MM-DD>  host=<OS/CPU>  checksum=<integer>
variants: <build commands, versions, PATH>
denominator: 1 warmup + N interleaved timed runs; median (min..max)
table: variant | median | min | max | ratio vs <explicit base>
base for ratios: <C+ legacy for safe-vs-legacy; plain C / Clang for older suite>
gates: checksum parity PASS; all exit codes 0; no build warnings
```

---

## 5. Concrete plan (ordered; implementation requires an owner with benchmark-file access)

This note does not change `benchmarks/**`. The following is the proposed work
for the agent who implements P2:

1. **Freeze the protocol** from §4 in one place (a single runner script) and
   delete the current split denominator (median of 4 non-interleaved runs in
   `safety_compare/run_bench.sh` vs median of 5 in `benchmarks/run_bench.sh` vs
   median of 7 interleaved in the ad-hoc modern run) by re-running all rows under
   the single rule: 1 warmup + ≥7 interleaved runs, median/min/max recorded.
2. **Re-run the integer workloads** (`loop.c`, `nested.c`) with the §4 harness,
   including the Clang `-O2` reference built by `/usr/bin/clang -O2`. Keep the
   "loops folded into closed form" caveat attached to these rows.
3. **Re-run the modern workload** (`modern.cp/.rs/.goose`) with the §4 harness,
   preserving the safe-vs-legacy pair as the headline ratio and the exact
   checksum `2200000200000` as a gate.
4. **Add a Clang `-O2` column for the modern workload only if** a plain-C
   equivalent is written and declared as a distinct workload (same operations,
   same checksum). Otherwise explicitly leave the Clang cell as "no equivalent
   plain-C source" rather than substituting C+ legacy and calling it Clang.
5. **Keep Rust and Goose as labelled cross-checks** with absolute-path builds;
   never convert their medians into "language X is faster" statements.
6. **Add Fil-C only when a toolchain exists**: install/build a Fil-C compiler +
   runtime, document its exact command and version, compile the identical
   workload, verify checksum parity, then time it under the same interleaved
   harness and report its median/ratio. Until then every Fil-C cell is
   literally `not measured (no filc toolchain installed)`.
7. **Add Windows/cross-OS rows only when a runner exists**: restore or supply
   `tools/build-win32-dll.sh`, a Windows x86 target, and an execution path
   (e.g. Wine with a documented, non-hard-coded prefix supplied via environment
   variables); then repeat the same workload and checksum gate. No Windows
   number may be inferred from the macOS arm64 numbers.
8. **Publish** the resulting table with commands, versions, host and denominator
   in `benchmarks/safety_compare/RESULTS.md`, and update the P2 status line in
   `docs/libc/AGENT1_PRIORITY_HANDOFF.md` only after the new measurements exist.
   (Both edits are outside this note's file ownership and are not done here.)

---

## 6. Exact blockers

### Fil-C (cannot produce a number today)

- **No toolchain**: `which filc` → "filc not found"; `RESULTS.md` states "no
  `filc` toolchain is installed"; `docs/FOUNDATION.md` line 11 states "no build
  of it exists in this environment."
- **Platform mismatch**: `docs/stdlib/upstream_study/fil_c.md` §8 records an
  ELF-based Linux stack (`ld-fil1-x86_64.so`, `ld-fil1-aarch64.so`, glibc 2.40,
  musl, pizfix, `/opt/fil`, Pizlix); Mach-O is described as previously
  demonstrated, not as a supported build here. This host is Darwin/macOS arm64.
- **No 32-bit support**: the same section quotes `invisicaps.html` — InvisiCaps
  "would allow for 32-bit pointers if Fil-C supported 32-bit systems" — making
  the repo's x86 Windows ABI target unsupported by design.
- **Whole-program ABI**: Fil-C has its own ABI, mangled `pizlonated_` symbols,
  getter indirection, and "no interoperability with Yolo-C"; the whole program
  and all dependencies must be built with Fil-C. C+ lowered C cannot simply be
  substituted, and DLL injection into an unmodified retail EXE cannot satisfy it.
- **Missing runtime components**: no `llvm::FilPizlonator` pass, no
  `libpizlo.so`, no `filc_crt.o`, no Fil-C loader, no pizfix slice, no rebuilt
  libc/libc++; raw `stdfil.h` `#error`s unless `__PIZLONATOR_WAS_HERE__` is
  defined by the Fil-C compiler.
- **Decision context**: `docs/UPSTREAM_AUDIT.md`, `docs/AUDIT.md` and
  `docs/libc/UPSTREAM_EVALUATION.md` reject Fil-C as a foundation (full Clang
  fork, mandatory GC, custom ABI), so there is no plan to vendor or build it.
  Any future evaluation would be as an optional backend for the same lowered C
  source (`docs/FOUNDATION.md` lines 54–56), never a fabricated measurement.

### Clang (toolchain present; comparability is the blocker, not availability)

- Clang **is** the backend and the reference compiler: `/usr/bin/clang` 16.0.0
  (`clang-1600.0.26.6`). The existing Clang `-O2` numbers are in
  `benchmarks/safety_compare/RESULTS.md` (as "plain C -O2", loop/nested only)
  and `docs/LIMITATIONS.md` (8 microbenchmarks).
- **Denominator mismatch**: those tables use median-of-4 (5 runs, first
  discarded) and median-of-5 (via `timeit.py`) with no interleaving, while the
  modern workload uses median-of-7 interleaved. They cannot be merged into one
  table without re-running under §4.
- **No Clang column for the modern workload**: `modern.cp` is expressed in C+
  stdlib types and has no plain-C equivalent, so no honest Clang `-O2` row exists
  for it yet.
- **Compiler pinning**: `docs/LIMITATIONS.md` lines 263–265 record that
  Homebrew LLVM `clang` in this environment fails at runtime with a missing
  `@rpath/libLLVM.23.1.dylib`; the reference compiler must be `/usr/bin/clang`,
  not an arbitrary `clang` on `PATH`. Any re-run must pin the absolute path.
- **No Windows Clang results**: no Windows target build exists in this checkout.

### Windows (no runner, no target, no toolchain)

- `tools/build-win32-dll.sh` — named in AGENTS.md, the handoff prompt and
  `docs/stdlib/upstream_study/fil_c.md` line 370 — **does not exist** here;
  `tools/` contains only `verify.sh`.
- No Windows host, Wine prefix, or retail executable is available or may be
  hard-coded; the existing scripts are POSIX `sh` and pin `/usr/bin/clang`,
  `/bin`, `/opt/homebrew/bin`, etc.
- Fil-C specifically cannot target this platform (no 32-bit support, ELF/ABI
  requirements, whole-program rule) — see Fil-C blockers above.
- Cross-OS: only macOS arm64 (Darwin 27.2.0, Apple M3 Pro T8112) numbers exist;
  Linux CI jobs are UNVERIFIED remotely per
  `docs/libc/AGENT1_PRIORITY_HANDOFF.md` line 23. No cross-OS comparison can be
  claimed from the recorded evidence.

---

## 7. Handoff summary

- Document: `tests/stdlib/agent1_handoff/performance/NOTES.md` (this file only;
  no other file was created, modified, staged or committed).
- Evidence: all medians in §2 are transcribed from
  `benchmarks/safety_compare/RESULTS.md` and `docs/LIMITATIONS.md` with their
  recorded commands, host, versions, workload constants and run denominators.
  No new benchmark was run and no number was invented.
- Protocol: §4 (identical workload + checksum parity, one warmup, ≥7 interleaved
  runs, median/min/max, explicit ratio base, pinned compilers); plan in §5.
- Blockers, exactly: **Fil-C** — no `filc` toolchain/build here, ELF/Linux +
  Mach-O only (no 32-bit support), whole-program custom ABI incompatible with
  DLL injection, and absent pass/runtime/loader components; **Clang** — present
  and measured, but its existing numbers use different non-interleaved
  denominators, lack a modern-workload column, and require `/usr/bin/clang`
  pinning because Homebrew clang is broken here; **Windows** — no
  `tools/build-win32-dll.sh`, no Windows/x86 runner or execution environment,
  and Fil-C cannot target x86 Windows; **cross-OS** — macOS arm64 only, Linux CI
  unverified.
- Interpretation guardrail: C+ safe vs C+ legacy is the only apples-to-apples
  pair; Rust/Goose/Clang numbers are cross-checks or reference points, not
  evidence of language superiority; "not measured" is the only truthful Fil-C
  entry until a toolchain exists.
