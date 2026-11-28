# P2 handoff — Fuzzing and compiler differential testing

Branch: `agent/modern-stdlib`. Date: 2026-11-23.

Documentation-only handoff. **No differential harness exists in this repository,
and none is created by this note.** Nothing here claims a live measurement, a
percentage, or a compiler fix. Only the two files owned by this handoff
(`NOTES.md`, `manifest.json`) were written; no compiler, runtime, test source,
or manifest was edited; no commit was made.

## 1. What P2 actually asks for

`docs/libc/AGENT1_PRIORITY_HANDOFF.md:22` defines the gap as:

> "stdlib fuzz 1200 cases, libc fuzz 4800, rust-vs-C+ 600; no compiler
> differential false-negative measurement"

and `docs/libc/AGENT1_PRIORITY_HANDOFF.md:60-62` defines the P2 done-criterion:

> "a differential harness measures false negatives against randomized valid
> programs"

So P2 is **not** another runtime fuzzer and **not** another list of hand-picked
reproducers. It is a *compiler-side* differential experiment: generate programs
from a seeded generator, attach an oracle label (safe / seeded violation), run
the compiler front end on each, and report false-negative (and false-positive)
counts with explicit denominators.

Relevant compiler flow: `docs/ARCHITECTURE.md:49-51` states that stage 5 writes a
temporary lowered `.c` with `-include cplus/cplus.h` and invokes the C compiler,
and "for `run`, execute the produced binary; for `check`, stop at -fsyntax-only".
The lowered C is inspectable via `--emit-c` (`docs/ARCHITECTURE.md:54-57`,
`docs/LANGUAGE.md:244-247`). This gives a well-defined pair of commands to
compare: C+ front-end checking of the `.cp` source and Clang syntax-only
checking of the lowered C.

## 2. Existing assets — exact counts, seeds, and what each measures

Path correction: there is no `tests/compile_time/` directory. The compile-time
suite lives at `tests/stdlib/compile_time/`. The "52 cases" figure belongs to
that suite, not to `tests/compile_fail/` (98 source files).

| Asset | Exact size / seed | What it actually measures | Compiler differential? |
| --- | --- | --- | --- |
| `tests/stdlib/fuzz/fuzz_hp.cp` (`run_fuzz.sh` is a separate libFuzzer runner for `fuzz_json`/`fuzz_string`/`fuzz_containers`) | `CASES 1200` (line 36); LCG seed `0xC0FFEE`, recurrence `6364136223846793005 / 1442695040888963407`, take `state >> 33` (lines 46-51); expected digest `4237bdfb74e154a4` (`docs/stdlib/HP_FUZZ_NOTES.md:145,199`) | **Runtime** correctness of `.hp` containers (string, vector, intmap/intset/strmap, deque, list, stack, queue, io/error) on 1200 *valid* operations, checked against independent shadow models after every op; FNV digest is a regression oracle; all inputs bounded, deliberately no UB (`fuzz_hp.cp:1-25`). `c+ check` appears only as an optional one-shot warning check (`HP_FUZZ_NOTES.md:134-139,202`). | **No.** One program, one fixed schedule; no accept/reject comparison, no diagnostics under test. |
| `tests/libc/fuzz_libc.c` | `300 iterations x 8 API calls = 2400` per pass, `x 2` logging passes = **4800 operations** (lines 44-48); LCG seed `0x5EED1234`, `1664525 / 1013904223` (lines 27-35); digest `2b62150fb0777106` (`docs/libc/LIBC_FUZZ_NOTES.md:96-101`) | **Runtime** correctness of 8 hardened libc string/memory APIs using 8-byte canaries and untouched-tail checks, plus proof that the opt-in logger is observationally inert (identical digest with logging off/on). All calls in-bounds; invalid pointers/huge `n` excluded (`LIBC_FUZZ_NOTES.md:34-68`). Clang appears only as the build tool with `-Wall -Wextra -pedantic` and UBSan (`LIBC_FUZZ_NOTES.md:74-90`). | **No.** Clang is not used as a reference oracle; no compiler diagnostics are compared. |
| `tests/rust_vs_cplus/` (`run_compare.sh`) | 6 families (alloc, memory, stdio, conversion, math, concurrency) x 100 scenarios = **600** comparisons (lines 21-22, 42-54) | **Cross-language runtime outcome** comparison: first `RESULT ` token `ACCEPTED`/`REFUSED`/`SKIP`; mismatches are recorded as documented differences, not failures; protocol violations and compile errors are failures (lines 3-6, 43-59). The script even warns that outcome equality does not prove equivalent safety (line 63). | **No.** This is runtime behaviour vs Rust, not C+ front end vs Clang, and it emits no diagnostic-equivalence result. |
| `tests/compile_fail/` + `tests/run_tests.sh:84-112` | **98** source files (97 `.c` + 1 `.cp`), each paired with a `.err` file containing an expected diagnostic *substring* | Hand-written negative programs. For each, `c+ check <src>` must exit nonzero and the log must contain the `.err` substring (`run_tests.sh:86-111`; one documented warning-only exception at lines 95-99). This is a golden-substring regression suite, not a generated corpus. | **No.** Fixed inputs, no Clang comparison, no seeded generator, no rate with a denominator. |
| `tests/stdlib/compile_time/` (`run_compile_time.sh`) | **8 manifests / 52 cases**: 9 expected REJECT, 19 expected ACCEPT, 24 expected RUNTIME (`docs/libc/COMPILE_TIME_GAPS.md:9-20`). Observed normalized: **5** tagged `[CPLUS-` REJECT, **7** untagged PARTIAL, **40** compiles-clean; **27** declared gaps (`AGENT1_PRIORITY_HANDOFF.md:26-31`; `COMPILE_TIME_GAPS.md:133-158`). | Per case, two commands: `c+ check --strict` and `c+ build --strict --sanitize=none` (`run_compile_time.sh:38-48`). A rejection counts as C+ enforcement **only** if the output contains a `[CPLUS-` token (line 42, rule restated at `COMPILE_TIME_GAPS.md:22-27,52-62`); untagged rejections are reported as `GAP-UNTAGGED` and never as passes (lines 49-67, 77-81). | **Partial, and not differential.** It checks front end vs backend *within* C+ and tags diagnostics, but inputs are hand-written manifests, there is no Clang `-fsyntax-only` reference, and it reports counts, not a false-negative *rate* over a generated population. |
| `tests/scenarios/scenario_06.c` (stack-escape probe) | 1200 enumerated scenarios; `c+ check --default` emits **695** `[CPLUS-` diagnostics covering **653** cases; **547** undiagnosed (368 + 138 + 41) (`docs/libc/KNOWN_COMPILER_GAPS.md:64-81`) | A one-shot, hand-enumerated false-negative inventory with a real denominator. | **No.** It is enumerated, not randomized, and has no C+-vs-Clang parity check. Useful as a prior for what a differential harness should discover. |

Summary of the existing 1200 + 4800 + 600 figure: those three suites total 6600
*runtime* checks (plus 98 golden compile-fail cases and 52 compile-time cases),
but **none of them executes a generated program through two compilers and
reports an accept/reject or diagnostic-classification divergence.**

## 3. Concretely what is missing

1. **A seeded program generator feeding the compiler front end.** Existing
   generators either generate *operation streams inside one already-valid
   program* (`fuzz_hp.cp`, `fuzz_libc.c`) or generate *runtime scenarios*
   (`rust_vs_cplus`). There is no generator that emits `.cp`/`.c` translation
   units whose compile-time acceptance is the observable under test.

2. **A c+ check vs Clang syntax-only differential oracle.** `c+ check` stops at
   `-fsyntax-only` (`ARCHITECTURE.md:51`) on the lowered C; no script compares
   its accept/reject decision with `/usr/bin/clang -fsyntax-only` run on the
   emitted C (`c+ build <src> --emit-c out.c --keep-temp -o out`,
   `docs/LANGUAGE.md:244-247`). The only scripted `-fsyntax-only` mention under
   `tests/` is in a note, not a harness (`tests/adversarial/concurrency_attack_NOTES.md`).

3. **Diagnostic-equivalence expectation, not just substring presence.**
   `tests/run_tests.sh:104-109` asks "does the log contain this string?" and
   `run_compile_time.sh:42` asks "is there any `[CPLUS-` token?". Neither checks
   that the tagged diagnostic *names the same invalid operation* the oracle
   seeded, nor that a SAFE control produces zero `[CPLUS-` diagnostics.

4. **False-negative measurement with a denominator.** The repository records
   false negatives as fixed enumerated counts (547 undiagnosed in
   `KNOWN_COMPILER_GAPS.md:64-81`; 27 declared gaps in
   `COMPILE_TIME_GAPS.md`), not as
   `accepted-unsafe / total-unsafe` over a randomized population. Ignoring a
   seeded violation is precisely the miss that matters for "make the compiler
   enforce safety" (`AGENT1_PRIORITY_HANDOFF.md:3-7`).

5. **False-positive measurement on randomized valid programs.** The 19 expected
   ACCEPT compile-time cases are fixed controls (`COMPILE_TIME_GAPS.md:9-20`);
   there is no seeded generator producing many *valid* programs to measure C+
   false rejections relative to Clang.

6. **Mode delta and determinism controls.** No asset compares `--default` vs
   `--strict` on the same generated corpus, and none records the generator seed
   plus both compilers' versions in a machine-readable result file.

7. **No invented interpretation layer.** Text near the existing 80% coverage
   gate explicitly warns that its numerator/denominator are name-presence in
   text, not execution or behaviour (`docs/stdlib/COVERAGE_TOOL_LIMITATIONS.md:63-71,73-104`).
   The P2 report must be at least as explicit about its own denominator.

## 4. Proposed minimal differential harness (DESIGN ONLY — DOES NOT EXIST)

Nothing in this section has been implemented or run. These are proposed files
and commands, included so the handoff is reproducible:

- Proposed generator: `tests/stdlib/agent1_handoff/fuzz_diff/gen_cases.py`
  (not created), fixed seed recorded in output, emitting `case_0001.cp ...` plus
  `cases.csv` with columns `id,seed,label,family,seeded_violation`. Labels:
  `SAFE` (no injected violation) and `UNSAFE_<KIND>` with exactly one seeded
  violation per program from a small kind set (use-after-free, double-free,
  direct fixed-array OOB subscript, use-after-move, mutable-alias escape). The
  SAFE programs are the "randomized valid programs" named in
  `AGENT1_PRIORITY_HANDOFF.md:60`.
- Proposed driver: `tests/stdlib/agent1_handoff/fuzz_diff/run_diff.sh`
  (not created), following existing harness conventions: `CPLUS_BIN` /
  `CPLUS_CLANG` env overrides, `timeout`, separate stdout/stderr/exit code, no
  hidden pipelines, absolute `ROOT` (`tests/run_tests.sh:17-18,15`;
  `tests/stdlib/fuzz/run_fuzz.sh:5-10`; `docs/stdlib/HP_FUZZ_NOTES.md:173-184`):

  ```sh
  # illustrative only; no such script is checked in
  CPLUS=${CPLUS_BIN:-"$PWD/build/c+"}
  CLANG=${CPLUS_CLANG:-/usr/bin/clang}
  OUT=build/fuzz-diff            # proposed output directory

  for src in "$OUT"/cases/*.cp; do
    id=$(basename "$src" .cp)
    timeout 10 "$CPLUS" check --strict "$src" >"$OUT/$id.cplus.log" 2>&1; rc_c=$?
    "$CPLUS" build "$src" --emit-c "$OUT/$id.c" --keep-temp \
        -o "$OUT/$id.bin" >"$OUT/$id.emit.log" 2>&1
    timeout 10 "$CLANG" -fsyntax-only -std=c11 \
        -include cplus/cplus.h "$OUT/$id.c" >"$OUT/$id.clang.log" 2>&1; rc_clang=$?
    grep -qF '[CPLUS-' "$OUT/$id.cplus.log" && tagged=1 || tagged=0
    # append id,label,rc_c,rc_clang,tagged to results.csv
  done
  ```

  The `-include cplus/cplus.h` on the Clang side mirrors the compiler's own
  stage 5 (`ARCHITECTURE.md:49-51`); exact include/define flags should be copied
  from a real `--emit-c` output before any run.
- Proposed reported fields (all computed from `results.csv` joined to
  `cases.csv`): generator seed, compiler versions, `N_safe`, `N_unsafe`,
  `N_total`, tagged-rejection counts, FP/FN counts, and parity mismatches.
  Print counts first; print a rate only with its numerator and denominator on
  the same line.

### Interpretation rules the proposed driver must enforce

- `SAFE` + C+ rejection with `[CPLUS-` diagnostic = **C+ false positive**.
- `SAFE` + Clang rejection (no C+ diagnostic) = generator/backend defect,
  reported separately, **not** a C+ finding.
- `UNSAFE_K` + C+ accept *or* rejection without a `[CPLUS-` diagnostic naming
  `K` = **C+ false negative** (this is the P2 quantity).
- `UNSAFE_K` + tagged rejection naming `K` = enforcement hit.
- `rc_c != rc_clang` alone is **not** a failure: C+ is expected to reject
  C+-specific violations that Clang accepts. Parity is reported as a
  *sanity/stratification* signal, never as ground truth.

## 5. No invented percentage — denominators

**No percentage is claimed by this document.** No differential harness has been
executed, so no false-negative or false-positive rate exists here. The only
numbers above are quoted verbatim from repository files with their locations;
where a file states a count (e.g. 5 tagged rejections of 52 cases), that is the
file's own recorded observation, not a result of this handoff.

The denominators a future P2 report must name are:

- `N_safe` — generated programs with oracle label SAFE. Denominator of the
  false-positive rate `FP_count / N_safe`.
- `N_unsafe` — generated programs with exactly one seeded, labelled violation.
  Denominator of the false-negative rate `FN_count / N_unsafe`.
- `N_total = N_safe + N_unsafe` — denominator of any overall accept/reject or
  parity-mismatch rate.
- `N_unsafe_K` — subset seeded with violation kind `K`, for per-kind rates
  `FN_K / N_unsafe_K`.
- Per-run metadata: generator seed, `c+` version, Clang version, and the number
  of generated programs actually executed (deduplicated timeouts / crashes
  must be listed, not silently dropped).

Do not reuse the 1200 / 4800 / 600 runtime-case counts as denominators for a
compiler diagnostic rate; they count operations and runtime scenarios, not
compiled programs.

## 6. Evidence index

- `docs/libc/AGENT1_PRIORITY_HANDOFF.md:22,60-62` — P2 definition and done criterion.
- `docs/ARCHITECTURE.md:49-57` — `check` stops at `-fsyntax-only`; lowered C is inspectable.
- `docs/LANGUAGE.md:244-247` — `--emit-c` command form.
- `tests/stdlib/fuzz/fuzz_hp.cp:36,46-51,256-631`; `docs/stdlib/HP_FUZZ_NOTES.md:134-147,195-217`.
- `tests/libc/fuzz_libc.c:27-48,326-369`; `docs/libc/LIBC_FUZZ_NOTES.md:34-68,93-106`.
- `tests/rust_vs_cplus/run_compare.sh:3-6,21-22,42-63`.
- `tests/run_tests.sh:84-112`; `tests/compile_fail/` (98 `.c`/`.cp` + 98 `.err`).
- `tests/stdlib/compile_time/run_compile_time.sh:13-83`;
  `docs/libc/COMPILE_TIME_GAPS.md:9-20,22-27,52-62,133-158`.
- `docs/libc/KNOWN_COMPILER_GAPS.md:64-81` (1200-scenario false-negative inventory).
- `docs/stdlib/COVERAGE_TOOL_LIMITATIONS.md:63-104` (denominator discipline).
