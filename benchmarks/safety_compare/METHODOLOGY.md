# Fair C+ vs Rust benchmark — safety-comparison methodology

This document defines the protocol used by `benchmarks/safety_compare/` when a
C+ workload is compared against a Rust (or plain-C) workload. It applies to the
`loop`, `nested`, and `modern` workloads in this directory. Results from this
protocol are published in `RESULTS.md`; this file is the *how*, `RESULTS.md` is
the *what was observed*.

The goal is a **fair, reproducible speed/semantics cross-check** between two
independently written programs that do the same work. It is deliberately not a
proof of language-level safety equivalence (see [Non-claims](#non-claims)).

---

## 1. Fairness principles

1. **Same workload, same algorithm, same constants.** Each variant computes the
   same mathematical result with the same iteration counts, input sizes, and
   data types. Do not change an iteration count to make one language look
   better. `modern.cp` / `modern.rs` are the canonical example: both build a
   `2,000,000`-element owned integer vector, append `"bbbbbbbbbb"` `200,000`
   times to an owned string, and sum the same quantities.
2. **Outcome equality is a hard gate.** Every variant must print the **exact
   same checksum** on **every** run. If checksums differ, the comparison is
   invalid and must not be reported — fix the workload first. Known-good
   checksums for this directory:
   - `loop` (2e8 integer sum): `19999999900000000`
   - `nested` (4000², the `i*j - j*i` terms cancel): `16000000`
   - `modern` (vector + string): `2200000200000`
   - `bench_modern`: `5595356797869224`
   - `alloc_bench`: `4750706934088118880`

   The last two are recorded by the final driver run in `results/table.txt`;
   all five matched checksums are listed verbatim in §9.
3. **Same compiler optimisation intent.** Compare release builds against
   release builds (`-O2` / `-O`). Never compare an optimised Rust binary to a
   debug C+ binary. In particular a C+ binary at **`-O0`** against Rust **`-O`**
   is **not apples-to-apples** and must be labelled as such (this is the case
   in `ALLOC_COMPARE_NOTES.md`, whose 6.25× median ratio is not attributable to
   allocator design alone). The only fair C+ speed configuration in this
   repository is **`--release --strict --sanitize=none`** (`-O2`, strict
   diagnostics, no sanitizers). `--release --legacy-c` is legacy C mode (not
   memory safe) and is **not** a fair substitute for a C+ speed baseline; any
   row using it may be reported only with that caveat spelled out.
4. **Same host, same pass, same measurement tool.** All variants in a
   comparison are built and measured on one machine, interleaved in time.
5. **Separate the safety evidence from the speed ranking.** The UBSan-instrumented
   C+ build is *evidence* that the executed paths are free of UBSan-visible
   undefined behaviour. Its wall-clock time is **not** a like-for-like speed
   number and is never used to rank C+ against Rust.

---

## 2. Exact compiler commands

All commands are run from the repository root. The restricted `PATH` mirrors
`benchmarks/safety_compare/run_bench.sh` and pins the C+ backend to Apple
clang (`/usr/bin/clang`), so the two C+ variants differ only in safety mode.
Replace `<w>` with `loop`, `nested`, or `modern`. Output binaries go under
`build/bench_safety/`.

### 2.1 C+ "speed" configuration — strict, sanitizers off

This is the performance baseline for C+: fail-closed static diagnostics
(`--strict`) with **no** runtime sanitizer instrumentation (`--sanitize=none`)
and `-O2` release optimisation.

```sh
mkdir -p build/bench_safety
PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin \
  build/c+ build benchmarks/safety_compare/<w>.cp \
    --release --strict --sanitize=none \
    -o build/bench_safety/cplus_strict_<w>
```

> **Flag-availability note.** Some driver builds in this repository advertise a
> shorter option set (`--release`, `--debug`, `--legacy-c`, …) in `--help` and
> do not parse `--strict` / `--sanitize`. In that case the documented older
> no-sanitizer baseline is `build/c+ build --release --legacy-c <src>`, and the
> safe default (`build/c+ build --release <src>`, UBSan) is the safety build.
> If you must substitute `--legacy-c` for `--strict --sanitize=none`, say so
> explicitly in `RESULTS.md` — the two modes are not semantically identical
> (legacy C mode is **not** memory safe), and `--strict --sanitize=none` is the
> **only fair C+ speed configuration** when supported. A `--legacy-c` row is a
> legacy-C reference, not a fair C+ speed row, and must never be used to claim
> C+ speed parity with Rust `-O`. Likewise do not silently drop `--release`: a
> C+ build at `-O0` against Rust `-O` (as in `ALLOC_COMPARE_NOTES.md`) is not
> apples-to-apples and must be reported with that caveat.

### 2.2 C+ "safety evidence" configuration — safe/default + UBSan

Same source, same `-O2`, but in Safe mode with the backend UBSan instrumentation
(`-fsanitize=undefined,bounds,null,alignment`,
`-fno-sanitize-recover=all`, `-fno-omit-frame-pointer`, `-DCPLUS_SAFE=1`,
per `compiler/Driver/Driver.cpp`). Run it, confirm exit code 0 and an empty
stderr (no sanitizer diagnostic), and confirm the checksum still matches.

```sh
PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin \
  build/c+ build benchmarks/safety_compare/<w>.cp \
    --release --strict --sanitize=undefined \
    -o build/bench_safety/cplus_ubsan_<w>
```

If the driver does not accept `--sanitize=undefined`, use the default safe
build `build/c+ build --release benchmarks/safety_compare/<w>.cp` (which already
enables UBSan in Safe mode) and record the substitution.

### 2.3 Rust release baseline

```sh
rustc -O benchmarks/safety_compare/<w>.rs \
  -o build/bench_safety/rust_<w>
```

### 2.4 Optional plain-C reference

```sh
/usr/bin/clang -O2 benchmarks/safety_compare/<w>.c \
  -o build/bench_safety/c_<w>
```

Plain C is an optimisation reference only; it has neither C+ diagnostics nor
Rust's safety model, so it is not part of the safety discussion.

> Goose (`goose build …`) and Fil-C are out of scope for a C+ vs Rust protocol.
> Goose results, when present in `RESULTS.md`, use a separately supplied path
> via `CPLUS_GOOSE`. Fil-C is **not measured** anywhere (no `filc` toolchain
> installed) and is never reported as a zero or a pass.

---

## 3. Runner protocol: warmup + interleaved runs

1. **Build all variants** with the commands above. Record build exit codes and
   stderr; a variant with a failed or warning-bearing build is not measured.
2. **One untimed warmup run per binary.** This absorbs page faults, dyld work,
   and CPU frequency ramp-up (same discipline as `benchmarks/timeit.py`).
3. **Interleaved timed runs.** Default: **7** timed runs per binary, in
   round-robin order across variants (`strict C+, UBSan C+, Rust, [C]` repeated
   7 times) so that all variants share the same frequency/thermal windows. A
   per-variant block of 7 runs is *not* equivalent and must not be substituted
   when drift is a concern.
4. **Record the checksum on every run** (capture stdout), not just the timing.
   Any mismatch invalidates that comparison.
5. **Repeat the whole pass** at least twice on different occasions. Report the
   inter-pass spread of ratios — on this shared host it is material (see the
   variance note in `RESULTS.md`).

Reference harness (extend as needed; this mirrors `RESULTS.md`):

```sh
python3 - <<'PY'
import subprocess, time, statistics, math, sys
out = 'build/bench_safety'
w = sys.argv[1] if len(sys.argv) > 1 else 'modern'
variants = ['cplus_strict', 'cplus_ubsan', 'rust']  # add 'c' if built
bins = {v: f'{out}/{v}_{w}' for v in variants}
CHECKSUM = None

# warmup
for v in variants:
    r = subprocess.run([bins[v]], capture_output=True, text=True, timeout=300)
    assert r.returncode == 0, (v, r.returncode, r.stderr)
    CHECKSUM = r.stdout.strip()

samples = {v: [] for v in variants}
for _ in range(7):                      # interleaved rounds
    for v in variants:
        t = time.perf_counter()
        r = subprocess.run([bins[v]], capture_output=True, text=True, timeout=300)
        samples[v].append(time.perf_counter() - t)
        assert r.returncode == 0 and r.stdout.strip() == CHECKSUM, (v, r.stdout)

def p95(a):
    a = sorted(a)
    k = max(1, math.ceil(0.95 * len(a)))   # nearest-rank
    return a[k - 1]

for v in variants:
    s = samples[v]
    line = (f'{v:14s} median={statistics.median(s):8.4f}s '
            f'min={min(s):.4f}s max={max(s):.4f}s ')
    line += f'p95={p95(s):.4f}s' if len(s) >= 20 else 'p95=n/a (n<20)'
    print(line)
print('checksum:', CHECKSUM)
PY
```

### Statistics to report

| Statistic | When | Notes |
|---|---|---|
| **median** | always | Primary number; robust to a single scheduler hiccup. |
| **min** | always | Best case; useful as a lower bound. |
| **max** | always | Surfaces stalls. |
| **p95** | only when `n >= 20` interleaved samples | With the default 7 runs, p95 degenerates to ~max, so report `p95=n/a` rather than a misleading number. Raise the run count when a tail estimate is actually wanted. |

Always report the **sample count `n`** and the **ratio of medians** against a
stated baseline (the strict C+ build for speed, with UBSan C+ shown
separately). Round ratios to two decimals and never more precision than the
run-to-run spread supports.

### 3.1 Row status definitions: PASS / FAIL / BLOCKED

The `status` column of `results/table.txt` uses the tokens `ok` and `blocked`.
They map to three row classifications, defined here so no status is mistaken
for a result:

| Status (token in `table.txt`) | Definition | Reported numbers |
|---|---|---|
| **PASS** (`ok`) | All 7 timed runs completed with exit code 0 **and** the expected checksum on every run (`run = 7/7`); median and p95 computed from the complete sample. | median/p95 in the row; eligible for comparison per §5. |
| **FAIL** | Build or run error, non-zero exit, checksum mismatch on any timed run, or a `run` count below `7/7` for reasons other than a documented blocker. | Row is invalid; median/p95 must **not** be reported. Diagnose and re-run; a FAIL is never silently relabelled PASS. |
| **BLOCKED** (`blocked`) | The binary cannot be built in this checkout because of an external toolchain/interface defect outside this repository (§7.4), not because a test failed. | `run = 0/7`, checksum/median/p95 shown as `-`. BLOCKED is **not** a pass, **not** a zero, and **not** evidence against the code under test. |

Distinct from these benchmark-row statuses is the safety-matrix outcome token
`COMPILE_ERROR` (a deliberate compile-only rejection, see
`SAFETY_MATRIX_NOTES.md`); it is not a benchmark row and is never counted in the
`ok`/`blocked` totals.

---

## 4. Time and RSS measurement tools

- **Wall-clock time:** `time.perf_counter()` around `subprocess.run(...)` (as in
  `benchmarks/timeit.py` and the harness above). Report wall-clock, not CPU
  time, unless CPU time is separately labelled.
- **Quick standalone median:** `python3 benchmarks/timeit.py <runs> <binary>`
  prints `median min` (6 decimals) after discarding one warmup run. It does
  **not** interleave, so prefer the harness above for cross-language claims.
- **Peak RSS (macOS):** `/usr/bin/time -l <binary>` prints `maximum resident set
  size` in **bytes**. Example:

  ```sh
  /usr/bin/time -l build/bench_safety/cplus_strict_modern 2>&1 \
    | grep 'maximum resident set size'
  ```

- **Peak RSS from Python:** `resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss`.
  Units differ by OS — **bytes on macOS, KiB on Linux** — so never compare RSS
  numbers captured on different operating systems without normalising.
- **RSS caveat:** for these tiny synthetic workloads, peak RSS is dominated by
  process/loader startup, so it is a **sanity check for pathological memory
  use**, not a discriminator between languages. Only draw memory conclusions
  from a workload whose live allocation dwarfs startup (e.g. an enlarged
  `modern`).
- **Hygiene:** run on AC power, no concurrent builds or fuzzers, same `PATH`
  and environment across variants, and record the toolchain versions (§6)
  alongside every result.

---

## 5. What "same or closer" means operationally

"S same or closer to Rust" (or to plain C) is a **measurement statement**, not a
guarantee. It is allowed only when **both** conditions hold:

1. **Outcome equality (mandatory).** Byte-identical checksum/stdout for every
   variant on every timed run, with exit code 0 and no sanitizer diagnostic in
   the safety build. This is the only sense in which the programs are "the
   same": same observed result on this workload.
2. **Similar medians (with an explicit denominator).** Let `T_X(w)` be the
   median wall-clock time of variant `X` on workload `w` (same `n`, same
   interleaved protocol, PASS rows only). Define the ratio with an explicitly
   named baseline `B` as

       R(X vs B; w) = T_X(w) / T_B(w)

   — numerator `T_X`, denominator `T_B`; the denominator is always stated. For
   a C+ vs Rust cross-check the baseline is Rust `rustc_O`, so
   `R = T_C+ / T_Rust`; for the two-C+ check the baseline is the non-instrumented
   C+ build, so `R = T_C+(UBSan) / T_C+(strict,none)`. "Same or closer" is
   allowed only when `0.80 ≤ R ≤ 1.25` (i.e. the numerator is within 1.25× of
   the denominator in either direction; `1/1.25 = 0.80`), the ordering of
   variants is stable across repeated passes, and the min/max ranges overlap.
   If `R < 0.80` or `R > 1.25`, report "different"/"faster"/"slower" with the
   actual ratio. Never use the UBSan-instrumented time as the C+ speed
   numerator, and never use an `-O0` C+ numerator against a Rust `-O`
   denominator.

Anything outside that definition must be phrased as "different", with the
observed ratio. **Never** paraphrase "same checksum and similar medians" as
"equivalent safety", "Rust-level safety", "equivalent guarantees", or "C+ is
as safe as Rust": the two languages enforce different contracts by different
mechanisms (§7.1).

Note that the UBSan build is *expected* to be slower because of instrumentation
(the recorded overheads range from ~2.3× on `modern` to ~30× on the integer
`loop`). Such a gap is instrumentation cost, **not** evidence that safe C+ is a
different language or a correctness failure.

---

## 6. Recorded toolchain and host facts

Checked on this host at document creation time:

- **rustc:** `1.100.0 (0ef1bc386 2026-11-09)`, host `aarch64-apple-darwin`,
  LLVM `23.1.5`; invoked as `rustc -O`.
- **C+ backend C compiler:** Apple clang `16.0.0 (clang-1600.0.26.6)`,
  target `arm64-apple-darwin27.2.0`, pinned via `/usr/bin/clang` using the
  restricted `PATH`. (`which clang` on the default interactive PATH points at
  Homebrew LLVM, which is **not** the backend used by these benchmarks.)
- **C+ driver:** `build/c+`, version `C+ 0.1.0` (Backend: host Clang/LLVM).
- **OS:** macOS `27.2.1` (build `26B131`), Darwin `27.2.0`,
  kernel `root:xnu-13432.41.4~25/RELEASE_ARM64_T8112`.
- **CPU / machine:** Apple M2 (`arm64`, `hw.model = Mac14,7`), 8 logical cores,
  8 GiB RAM. (`RESULTS.md` previously labelled this T8112 host "Apple M3 Pro";
  the current `sysctl` values above are the authoritative host record.)

Re-check these before publishing new numbers; a toolchain or machine change
invalidates direct comparison with older rows in `RESULTS.md`.

---

## 7. Non-claims and known blockers

These limitations are part of the methodology. Any report that omits them is
incomplete.

1. **No Rust-equivalence proof.** Rust's compile-time ownership/borrow/type
   checks and C+'s static diagnostics + runtime UBSan checks are different
   mechanisms with different coverage. Identical checksums and similar medians
   on these workloads are **not** proof that C+ provides Rust-equivalent memory
   or thread safety, nor that the two languages have equivalent guarantees.
2. **Sanitizer success is not proof of safety.** "UBSan clean" means only "no
   UBSan-detected undefined behaviour on the paths this workload executed, on
   this host, with this toolchain." It says nothing about unexecuted paths,
   logic errors, concurrency, or classes of UB UBSan cannot see. A clean UBSan
   run is evidence, not a certificate.
3. **ASan is unsupported here.** The backend compile-and-run probe rejects
   `address`, `leak`, `thread`, `memory`, and `cfi` sanitizers (error shape:
   `error: sanitizer 'address' is not supported by the selected backend
   (/usr/bin/clang); detected by a compile-and-run probe`). Do not report ASan
   results, and do not treat the absence of an ASan run as a pass. Only
   `none`, `undefined` (UBSan), and `integer` are accepted by the probe.
4. **`CPLUS_EXIT_ARITHMETIC` is an external blocker.** Instrumented builds
   compile the external runtime
   (`/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/cplus_runtime.c`,
   which calls `cplus_print_fatal(CPLUS_EXIT_ARITHMETIC, "signed integer
   overflow", …)`) against this branch's local header
   (`runtime/include/cplus/cplus.h`), whose `cplus_exit_code` enum ends at
   `CPLUS_EXIT_IO = 109` and does not define `CPLUS_EXIT_ARITHMETIC`. Builds
   needing that instrumented runtime therefore fail (`use of undeclared
   identifier 'CPLUS_EXIT_ARITHMETIC'`) and are **BLOCKED**, not failed tests
   and not passes. This is the blocker documented in
   `docs/stdlib/AGENT1_EXTERNAL_RUNTIME_BLOCKER.md`. Do not claim sanitizer or
   runtime-arithmetic evidence for any path gated by it; record `BLOCKED`.
   (`--strict --sanitize=none` does not compile the instrumented runtime, which
   is one reason the speed configuration is usable here while some instrumented
   configurations are not.)

   **Exact failure shape and affected rows.** The mismatch is between the
   sibling `c-plus-core` runtime *source* and this branch's local *header*:
   `cplus_runtime.c:130` calls
   `cplus_print_fatal(CPLUS_EXIT_ARITHMETIC, "signed integer overflow", …)`,
   while `runtime/include/cplus/cplus.h` defines `cplus_exit_code` only through
   `CPLUS_EXIT_IO = 109` (the sibling header defines `CPLUS_EXIT_ARITHMETIC` as
   `110`). The build aborts with
   `runtime/cplus_runtime.c:130:23: error: use of undeclared identifier 'CPLUS_EXIT_ARITHMETIC'`
   followed by `error: failed to compile instrumented C+ runtime`. In the final
   reproducible driver run this BLOCKS every `cplus_safe` row in
   `results/table.txt` (`alloc_bench`, `bench_modern`, `modern`,
   `safety_matrix/matrix`, `validate_results/…/evidence.csv`): `run = 0/7`,
   checksum/median/p95 all `-`. A build-only
   `-DCPLUS_EXIT_ARITHMETIC=110` shim was used for the specific observations in
   `SAFETY_MATRIX_NOTES.md` and the resource-exhaustion UBSan evidence; it is a
   workaround, not a pass of the unmodified toolchain, and the final driver does
   not use it.
5. **Synthetic and machine-specific.** These are a handful of integer/allocation
   workloads on one machine. Optimisers may fold arithmetic loops into closed
   forms (identical checksums then prove semantics, not loop execution speed).
   Results are not general performance claims about any language or stdlib.
6. **Fil-C not measured.** No `filc` toolchain is installed; report it as
   "not measured", never as zero or a pass.
7. **No live-game / production claim.** These are host-side synthetic binaries;
   nothing here validates behaviour in the retail game or any deployed setting.

---

## 8. Reporting checklist

Before publishing a row in `RESULTS.md`, confirm:

- [ ] Identical workload source constants; checksum identical on every run.
- [ ] Exact build commands and toolchain versions recorded (§2, §6).
- [ ] Warmup performed; runs interleaved; `n` stated; median/min/max reported
      (p95 only if `n >= 20`).
- [ ] Time and RSS tools named; RSS units normalised if compared.
- [ ] UBSan C+ result recorded as safety evidence with exit code and stderr.
- [ ] "Same or closer" used only per §5, with numerator and denominator named;
      no equivalence language.
- [ ] Row status assigned per §3.1 (`ok` = PASS, `blocked` = BLOCKED, failures
      never reported as numbers); BLOCKED rows not counted as passes or zeros.
- [ ] C+ speed config is `--release --strict --sanitize=none`; `-O0` and
      `--legacy-c` caveats stated wherever those variants appear.
- [ ] Non-claims §7 restated or linked, including ASan unsupported and the
      `CPLUS_EXIT_ARITHMETIC` blocker status at the time of the run.

---

## 9. Final driver numbers (verbatim from `results/table.txt`)

`sh benchmarks/safety_compare/run_all.sh` produced
`ok=15 blocked=5 fail=0 required_failures=0` (driver exit 0) and
`BENCH_VALID 157 0` from `validate_results.cp`. The complete final table is
reproduced verbatim below; status tokens are defined in §3.1.

```
workload               language build            run     checksum                 median        p95 status  
---------------------- -------- ---------------- ------- -------------------- ---------- ---------- --------
alloc_bench            rust   rustc_O          7/7     4750706934088118880    0.010404   0.013873 ok      
alloc_bench            c+     cplus_safe       0/7     -                             -          - blocked 
alloc_bench            c+     cplus_legacy     7/7     4750706934088118880    0.039720   0.049571 ok      
bench_modern           rust   rustc_O          7/7     5595356797869224       0.023018   0.026586 ok      
bench_modern           c+     cplus_safe       0/7     -                             -          - blocked 
bench_modern           c+     cplus_legacy     7/7     5595356797869224       0.020533   0.024590 ok      
legacy_speed/loop      c      clang_O2         7/7     19999999900000000      0.002890   0.003327 ok      
legacy_speed/nested    c      clang_O2         7/7     16000000               0.004658   0.008925 ok      
loop                   c      clang_O2         7/7     19999999900000000      0.005660   0.006696 ok      
loop                   rust   rustc_O          7/7     19999999900000000      0.005310   0.008098 ok      
modern                 rust   rustc_O          7/7     2200000200000          0.009449   0.011401 ok      
modern                 c+     cplus_safe       0/7     -                             -          - blocked 
modern                 c+     cplus_legacy     7/7     2200000200000          0.008306   0.010272 ok      
nested                 c      clang_O2         7/7     16000000               0.004812   0.009405 ok      
nested                 rust   rustc_O          7/7     16000000               0.003341   0.006325 ok      
safety_matrix          rust   rustc_O          7/7     -                      0.005170   0.014102 ok      
safety_matrix/matrix   c+     cplus_safe       0/7     -                             -          - blocked 
safety_matrix/matrix   c+     cplus_legacy     7/7     -                      0.006687   0.014287 ok      
validate_results//Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/benchmarks/safety_compare/results/evidence.csv c+     cplus_safe       0/7     -                             -          - blocked 
validate_results//Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/benchmarks/safety_compare/results/evidence.csv c+     cplus_legacy     7/7     -                      0.004316   0.008333 ok      
```

Reading notes:

- Matched checksums: `loop=19999999900000000`, `nested=16000000`,
  `modern=2200000200000`, `bench_modern=5595356797869224`,
  `alloc_bench=4750706934088118880`.
- All five `cplus_safe` rows are **BLOCKED** (§7.4): no C+ safe-mode timing or
  checksum is claimed from this driver.
- The `cplus_legacy` rows are `--release --legacy-c` legacy-C references,
  **not** the fair C+ speed configuration `--release --strict --sanitize=none`
  (§1.3, §2.1). Do not present them as fair C+ vs Rust speed parity.
- `safety_matrix` and `validate_results` rows have no cross-language checksum
  (`-` in the checksum column); they are timing/status rows only.
- These are comparable observed throughputs for matched workloads, not
  Rust-equivalent guarantees and not a memory-safety proof.
