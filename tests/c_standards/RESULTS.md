# C-standard support in the C+ compiler — aggregate results

Aggregated from the seven independently executed group reports under
`tests/c_standards/<group>/RESULTS.md`. Aggregation date: 2026-11-23 (UTC).
Branch: `agent/modern-stdlib`. This document reports **only observed results**;
every number below is traceable to a sibling group's `RESULTS.md` and raw logs.

**Bottom line:** with the shipped toolchain (Apple clang 16.0.0, `/usr/bin/clang`,
on macOS/arm64), the C+ driver accepts and correctly forwards `-std=` for the
published ISO C editions C89/C90, C94 (amendment 1), C99, C11 and C17/C18. The
published name **C23 is not accepted** by this clang (`-std=c23` is an invalid
value); only the *draft* spellings `c2x`/`gnu2x` work, and C+ frontend coverage
of C23 language features is incomplete. **There is no "C21" standard.** **C2y /
C26 are drafts, not released standards.** Consequently the claim "C+ supports
every version of C ever released on earth" is **not supported by this evidence**:
C23-by-published-name fails at the backend and C23 draft features are partial.

---

## 1. Final table (required columns)

"Real projects compiled" counts C+ driver builds of the three unmodified
projects (`game_a/snake.c`, `game_b/2048.c`, `cli_app/xdump.c`) in the mode the
group used to make `-std` observable. "Runs matched" counts executed binaries
whose stdout/stderr/exit matched a reference build or recorded golden output.
"Failures" counts the failing cells/cases actually recorded in that group's
matrix. Denominators are stated explicitly; see §3 for per-group detail.

| Std family | Spellings tested | Real projects compiled | Runs matched | Failures |
|---|---|---|---|---|
| C89 / ISO C90 | `c89`, `c90`, `iso9899:1990`, `gnu89` (4) | none of the 3 real projects is genuine C89: all 3 strict-C89 builds rejected (also by reference clang). All 4 spellings build the genuine minimal C89 source 4/4. | 4/4 minimal stdout byte-identical to clang; 0/3 real (none is C89 source) | 3/3 strict-C89 real-source rejections (source is C99-era, matches clang); 1 strictness gap: C99 construct accepted under `-std=c89` without `-pedantic-errors` |
| C94 / C99 | `c94`, `iso9899:199409`, `c99`, `c9x`, `gnu99` (5) | Legacy mode, 5 spellings × 3 projects = 15 cells: 10 compiled (`c99`/`c9x`/`gnu99` 3+3+3; `iso9899:199409` 1 = 2048 only). Safe mode: 0/15 (C+ safety diagnostics). Minimal: 7/10 source-appropriate builds. | 10/10 compiled legacy cells matched golden; minimal source stdout matched in every accepted case | 5/15 legacy compile failures (incl. all `c94`, which is an invalid clang spelling); 15/15 Safe-mode rejections (non-std); `c94` unsupported |
| C11 | `c11`, `c1x`, `gnu11` (3) | 3/3 (`--legacy-c`, `-std=c11`); minimal 3/3 | Minimal 3/3 (`int counter=7`); real 12/13 byte-identical | 1/13 real run differs (2048 stdin board seeded from `time(NULL)`); 3/3 default Safe-mode rejections (safety diagnostics) |
| C17 / C18 | `c17`, `c18`, `iso9899:2017`, `iso9899:2018`, `gnu17` (5) | 3/3 (`--legacy-c`, `-std=c17`); minimal 5/5 clang and 5/5 C+ | Minimal 5/5; real 11/11 byte-identical | 0 tested-case failures; 3/3 default Safe-mode rejections noted as non-std |
| C23 (published name) | `c23`, `gnu23` (2) | 0/0 builds possible — clang 16 rejects both spellings | 0/0 | 2/2 unrecognized: `error: invalid value 'c23' in '-std=c23'`; forwarded verbatim by C+ |
| C2x (draft, = C23 draft mode) | `c2x`, `gnu2x` (2) | Legacy mode: 6/6 (2 spellings × 3 projects); minimal 2/2 build and run byte-identical to clang | 6/6 real project scenarios byte-identical; minimal stdout SHA-256 identical to clang | Feature matrix 3/6 clean C+ passes: `nullptr` rejected, `_BitInt` rejected, digit separators hang (10 s timeout, rc=124). Safe mode 6/6 rejections |
| C2y / C26 (draft only) | `c2y`, `c26`, `gnu2y`, `c2y_draft`, `c2b`, `iso9899:2024` (6, all rejected) | 0/0 — no flag exists | 0/0 | 6/6 unrecognized; no released standard under these names, so nothing to support |
| Real-project cross-check (all accepted spellings) | 11 accepted by clang: `c89`, `c90`, `c99`, `c11`, `c17`, `c2x`, `gnu89`, `gnu99`, `gnu11`, `gnu17`, `gnu2x` (`c94`, `c23` rejected) | Matrix A (prescribed command, Safe mode) 0/33; Matrix B (`--legacy-c`) 27/33 | Matrix A 0; Matrix B 27/27 compiled binaries ran and matched golden | Matrix A 33/33 failed (C+ safety gate, identical per project for every std); Matrix B 6/33 failed (pre-C99 `for(int i...)` in snake/xdump under `c89`/`c90`/`gnu89`). Combined 39/66 failures |

Aggregate runner result: **7 suites run, 7 passed, 0 failed, 0 pending** —
`cd tests/c_standards && sh run_all.sh` (full logs persisted with
`C_STANDARDS_LOG_DIR=<dir>`; see §4).

---

## 2. Toolchains

| Item | Exact value |
|---|---|
| C+ driver | `/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/c+` — `C+ 0.1.0`, backend host Clang/LLVM |
| C backend / reference compiler | `/usr/bin/clang` — Apple clang version 16.0.0 (`clang-1600.0.26.6`), target `arm64-apple-darwin27.2.0`, macOS SDK 15.2 |
| Host | macOS 27.2.0, arm64 (Apple M1) |
| `timeout` | `/usr/bin/timeout` (each group's own scripts use 10 s / 30 s / 60 s; this aggregate runner uses 300 s per suite) |
| Shells | `sh` and `bash` (per suite shebang; aggregate runner dispatches accordingly) |
| Real projects (unmodified, hash-verified) | `external_projects/game_a/original/snake.c` SHA-256 `13de0d13e1ee1e621c48d7f91ac283126a04c3980b0c22c4fafbf81d25bc5774`; `game_b/original/2048.c` `90f794c675a696aa10980dd120d1f0503ee4ed33a61fef92c7df8f443f5b18a8`; `cli_app/original/xdump.c` `93106c3b4b7bb4f0608bd3308621eca30d8ab7c4c7883b23f54d3c7fe0309253` |

Key mechanism (independently confirmed in three reports via `--verbose`):
the C+ driver hard-codes a backend default (`-std=gnu17` in Safe mode,
`-std=gnu89` in `--legacy-c` mode) and then appends the user's `-std=` token
**last**; clang's last-wins rule means the requested spelling is effective.
The driver performs no spelling validation — unsupported values are rejected
by clang, not normalized or silently ignored by C+. Tested spellings reach the
`clang -cc1` command line verbatim.

Two cross-cutting caveats (recorded, not counted as C-standard defects):
1. The driver does not pass `-pedantic-errors` by default. Under pre-C99
   spellings, C99 extensions are accepted as warnings unless the caller adds
   `-pedantic-errors`. Strict conformance checking is therefore not enabled by
   `-std=` alone.
2. C+'s C rewriter drops the final newline of sources ending in a single `\n`,
   so an *extra* `-pedantic-errors` can fail with
   `error: no newline at end of file [-Werror,-Wnewline-eof]` even for valid
   code. The prescribed driver command form is unaffected.
3. The default Safe mode rejects all three unmodified legacy C projects with
   C+ static-safety errors (`[CPLUS-BOUNDS]`, `[CPLUS-LIFETIME]`,
   `[CPLUS-OWNER]`, `[CPLUS-FFI]`) before the `-std` choice is decisive.
   The documented `--legacy-c` mode was used to expose C-version behavior;
   both results are reported, neither hidden.

---

## 3. Per-group evidence and exact reproduction

### C89 / C90 — `tests/c_standards/c89/`
- Report: `tests/c_standards/c89/RESULTS.md`; raw transcript
  `tests/c_standards/c89/run.log`; sources `hello89.c` (genuine ISO C90),
  `probe_c99.c` (negative control); captured stdout `stdout.ref`,
  `stdout.cplus.<spelling>`.
- Result: all four spellings build the genuine C89 program and produce stdout
  byte-identical to strict clang (SHA-256
  `3ef9faec661724985bd51ca5bfb9979612aca37e58172055ed66911b0e589536`, 167 B).
  `-std=c89` alone accepts the C99 mixed-declaration negative control (exit 0);
  adding `-pedantic-errors` rejects it exactly like clang (exit 1).
- No real project is genuine C89: strict clang rejects all three, and C+
  `--legacy-c -std=c89 -pedantic-errors` reproduces the same C99-extension
  errors. No live C89 real-project success is claimed.
- Reproduce: `sh tests/c_standards/c89/run.sh` (writes its log in that dir).
- Classification: **PARTIAL** (accepted and functional, but not enforced by
  the driver by default).

### C94 / C99 — `tests/c_standards/c99/`
- Report: `tests/c_standards/c99/RESULTS.md`; machine-readable
  `tests/c_standards/c99/logs/results.tsv` (124 rows) and
  `tests/c_standards/c99/logs/SUMMARY.txt`; per-case
  `logs/<case>.{cmd,stdout,stderr,exit}`; sources `c94_features.c`,
  `c99_features.c`; expected stdout under `expected/`.
- Result: `c99`/`c9x`/`gnu99` build both applicable minimal sources and all
  three real projects in legacy mode with 7/7 golden outputs matched each.
  `iso9899:199409` is valid and C94-conformant code works, but the default
  driver accepts C99 extensions; in legacy mode it builds 2048 (3/3 outputs
  match) while snake and xdump fail on genuine `redefinition of 'i'`.
  `c94` is **not** a clang spelling and fails everywhere.
- Reproduce: `bash tests/c_standards/c99/run.sh` (recreates `.work/` and `logs/`).
- Classification: `c99`/`c9x`/`gnu99` **SUPPORTED**; `iso9899:199409`
  **PARTIAL**; `c94` **UNSUPPORTED**.

### C11 — `tests/c_standards/c11/`
- Report: `tests/c_standards/c11/RESULTS.md`; raw transcript
  `tests/c_standards/c11/artifacts/run-report.log`; per-build logs and run
  captures under `tests/c_standards/c11/artifacts/`; source `source.c`
  (`<stdatomic.h>`, `_Static_assert`, `_Generic`).
- Result: `c11`/`c1x`/`gnu11` all build with zero diagnostics and run to
  `int counter=7`. A clang `-std=c99 -pedantic-errors` negative control rejects
  the source. All three real projects build via `--legacy-c -std=c11`; 12/13
  deterministic comparisons are byte-identical. The single differing case is
  game_b's stdin session, whose board is seeded from `time(NULL)` (both outputs
  are 1492 B with identical control-flow markers).
- Reproduce: `bash tests/c_standards/c11/run.sh`.
- Classification: all three spellings **SUPPORTED**.

### C17 / C18 — `tests/c_standards/c17/`
- Report: `tests/c_standards/c17/RESULTS.md`; `artifacts/results.tsv`,
  `artifacts/comparisons.txt`, per-spelling `{exit,stdout,stderr,run.*}` files;
  source `source.c` (`alignas`/`alignof`, `_Noreturn`, `_Static_assert`,
  `_Generic`, atomics).
- Result: all five spellings give clang 0 diagnostics, C+ 0 diagnostics, exit 0
  and expected stdout; 11/11 real-project comparisons byte-identical.
  `gnu17` is accepted but selects GNU extensions, not strict ISO C17.
  Negative controls: `-std=c89` and `-std=bogus` both fail as expected.
- Reproduce: `sh tests/c_standards/c17/run.sh`.
- Classification: all five **SUPPORTED** (`gnu17` with GNU-extension caveat).

### C23 (published name) and C2x (draft) — `tests/c_standards/c23/`
- Report: `tests/c_standards/c23/RESULTS.md`; raw captures under
  `tests/c_standards/c23/logs/`; source `source.c` (`__STDC_VERSION__ >=
  202000L` guard, `0b` literals, `typeof`, `#elifdef`). Expected output
  `STDC=202000 mask=10 branch=elifdef-taken`.
- Result: `c2x`/`gnu2x` accepted by clang 16 and by C+; minimal program output
  matches; all three real projects build in `--legacy-c` and match golden
  outputs byte-for-byte. `c23`/`gnu23` are rejected by clang 16
  (`error: invalid value 'c23' in '-std=c23'`) and C+ forwards them verbatim.
  `nullptr` is rejected by the C+ frontend at every spelling
  (`[CPLUS-FFI] C23 nullptr is not supported; use NULL`), so language coverage
  under a supported spelling is partial. Library coverage is partial: 31 C23
  symbols inventoried (`nextup`, `nextdown`, `fadd`/`fsub`/`fmul`/`fdiv`,
  `hypot3`, `_Float16` families, `timespec_getres`, `mbrtoc8`, `c8rtomb`);
  no full-coverage claim.
- Reproduce: `bash tests/c_standards/c23/run.sh`.
- Classification: `c2x`/`gnu2x` **SUPPORTED** with **PARTIAL** language/library
  coverage (this group's label); `c23`/`gnu23` **UNSUPPORTED (host toolchain)**.

### Latest spellings / C2y / C26 drafts — `tests/c_standards/latest/`
- Report: `tests/c_standards/latest/RESULTS.md`; raw captures
  `tests/c_standards/latest/.work/probe/<value>.err`,
  `.work/run/{minimal,feature_matrix,projects}.tsv` and
  `.work/run/p_<std>_<project>_<scenario>_<mode>.{out,err}`; source `source.c`.
- Result: only `c2x`/`gnu2x` are accepted; minimal program stdout is
  byte-identical between clang and C+ (SHA-256
  `4a4d37320c6e483af974714ab36ead024186f34429d643545c1b20424458ef9d`),
  `__STDC_VERSION__ == 202000L`. Rejected: `c23`, `gnu23`, `c2y`, `c26`,
  `iso9899:2024` (+ extra guesses `c2b`, `c2y_draft`, `gnu2y`). Feature matrix:
  `bool_true`, `binary_literal`, `typeof_operator` pass; `nullptr` and
  `_BitInt` are rejected by C+ (`[CPLUS-FFI]`); digit separators hang the
  frontend (rc=124 after 10 s). All 6 real-project legacy builds/scenarios
  match clang; Safe mode rejects all 6.
- Reproduce: `sh tests/c_standards/latest/probe.sh` then
  `sh tests/c_standards/latest/run.sh` (or just `sh .../run.sh`, which rewrites
  the report).
- Classification: `c2x`/`gnu2x` **PARTIAL** (this group's stricter label — the
  difference from the c23 group's "SUPPORTED with partial coverage" is a
  labelling threshold, not a disagreement about observed outcomes);
  `c23`/`gnu23`/`c2y`/`c26`/`iso9899:2024` **UNRECOGNIZED**.

### Real-project cross-check — `tests/c_standards/real_projects/`
- Report: `tests/c_standards/real_projects/RESULTS.md`; machine-readable
  `tests/c_standards/real_projects/logs/results.tsv`; per-cell evidence
  `logs/cells/<A|B>__<project>__<std>.evidence.txt`; raw runs `logs/runs/`.
- Result: Matrix A (prescribed command, default Safe mode): 0/33 compiled,
  33/33 failed with C+-safety diagnostics identical per project for every std —
  no C-version signal. Matrix B (`--legacy-c`): 27/33 compiled and 27/27 ran
  and matched golden; the 6 failures are genuine pre-C99 source violations
  (`redefinition of 'i'`) in snake and xdump under `c89`/`c90`/`gnu89`;
  2048 builds in all 11 modes. No `RUN_DIFFERENT` cases.
- Reproduce: `bash tests/c_standards/real_projects/run.sh`.
- Classification: C99-and-later spellings **SUPPORTED** for these three
  sources in `--legacy-c`; pre-C99 modes fail on the sources' own C99 syntax.

---

## 4. Aggregate runner and reproduction

```sh
# full aggregate (per suite: timeout 300, final 3 output lines captured)
cd tests/c_standards && sh run_all.sh

# persist full per-suite logs as evidence
C_STANDARDS_LOG_DIR=/path/to/logs sh tests/c_standards/run_all.sh

# individual groups (exact commands from each report)
sh   tests/c_standards/c89/run.sh
bash tests/c_standards/c99/run.sh
bash tests/c_standards/c11/run.sh
sh   tests/c_standards/c17/run.sh
bash tests/c_standards/c23/run.sh
sh   tests/c_standards/latest/run.sh
bash tests/c_standards/real_projects/run.sh
```

`run_all.sh` discovers every immediate subdirectory of `tests/c_standards/`
containing `run.sh`, dispatches to `bash` or `sh` according to that script's
shebang, enforces a 300-second timeout per suite, captures the full log and
prints its final three lines, then prints a pass/fail/pending count. It is pure
POSIX shell — no Python and no `|| true`. A missing `run.sh` is reported as
`PENDING` with its exact reproduction command (none remain pending).

---

## 5. Honest classification per published standard

Legend: ✓ = accepted spelling with observed working build+run evidence;
✗ = no working evidence / rejected; ~ = partial or caveated. "Working" means a
genuine source for that edition compiled *and ran* through the C+ driver,
with output checked against a reference or golden capture.

| Standard | Verdict | Evidence (exact) |
|---|---|---|
| **C89** (ISO/IEC 9899:1990) | **✓ with caveat (~)** | `c89`/`iso9899:1990`/`gnu89` build `hello89.c`, stdout byte-identical to clang in `tests/c_standards/c89/run.log`; caveat: driver does not itself enforce C90 (`probe_c99.c` accepted without `-pedantic-errors`). |
| **C90** (same edition, alternate name) | **✓ with caveat (~)** | `c90` spelling tested alongside `c89` in the same matrix; identical results. C89 and C90 are the same published standard, not two versions. |
| **C94** (ISO/IEC 9899:1990/Amd.1) | **✓ via `iso9899:199409` only; `c94` spelling ✗** | `tests/c_standards/c99/logs/results.tsv`: `c94_features.c` builds/runs and matches; `c94` rejected by clang (`invalid value 'c94'`). 2048 builds in `iso9899:199409`; snake/xdump fail on C99 for-loop scoping. |
| **C99** (ISO/IEC 9899:1999) | **✓** | `c99`/`c9x`/`gnu99` in `tests/c_standards/c99/logs/results.tsv` and 7/7 real-project outputs matched; `tests/c_standards/real_projects/logs/results.tsv` Matrix B 3/3 projects for each spelling. |
| **C11** (ISO/IEC 9899:2011) | **✓** | `c11`/`c1x`/`gnu11` build and run `source.c` → `int counter=7` (`c11/artifacts/run-report.log`); 3/3 real projects build; 12/13 run comparisons byte-identical. |
| **C17 / C18** (ISO/IEC 9899:2018) | **✓** | `c17`/`c18`/`iso9899:2017`/`iso9899:2018`/`gnu17` — clang 0 diagnostics, C+ 0 diagnostics, correct run (`c17/artifacts/results.tsv`); 11/11 real comparisons match. C17 and C18 are two names for the same edition. |
| **C23** (ISO/IEC 9899:2024) | **✗ by published name; ~ via draft spellings** | `c23`/`gnu23` rejected by clang 16 (`latest/.work/probe/c23.err`, `c23/logs/`). `c2x`/`gnu2x` (draft, `__STDC_VERSION__=202000L`) build and run minimal + 3 real projects, but C+ rejects `nullptr` and `_BitInt` and hangs on digit separators (`latest/.work/run/feature_matrix.tsv`). No claim of full C23 conformance. |
| **C2x** (draft mode, pre-publication C23) | **~ (partial)** | `c2x`/`gnu2x` accepted; minimal output byte-identical; 6/6 real-project scenarios match; feature matrix 3/6 clean passes, 2 rejections, 1 timeout (`latest/RESULTS.md` §4). |
| **C2y / C26** | **✗ — DRAFT, not a released standard** | `c2y`, `c26`, `gnu2y`, `c2y_draft`, `c2b` all rejected by clang 16 (`latest/.work/probe/*.err`). No published standard to support; no feature can be tested. |

### Explicit non-standards
- **There is no "C21".** No ISO C standard by that name exists and no compiler
  flag implements it. Any claim of "C21 support" would be meaningless.
- **C2y / C26 are draft work, not released standards.** They must not be counted
  among "versions of C ever released".
- **`c23`/`gnu23` are not available in the shipped clang 16** even though C23
  itself is published (ISO/IEC 9899:2024); only the older draft spelling `c2x`
  is reachable, and it reports the draft value `202000L`.

### Verdict on the headline claim
Do **not** state that C+ "supports every version of C ever released on earth".
The evidence supports only: *C89/C90, C94 (via `iso9899:199409`), C99, C11 and
C17/C18 spellings are accepted and produce correct binaries for the tested
sources; C23-by-published-name is unrecognized by the host compiler; C23 draft
mode works for a subset of features.* All failures and limitations above are
part of the result.

---

## 6. File ownership / scope of this aggregate

This aggregate touches only `tests/c_standards/run_all.sh`,
`tests/c_standards/RESULTS.md` and `tests/c_standards/STD_SUPPORT_MATRIX.md`.
All evidence files were produced by the seven sibling groups inside their own
subdirectories. No source edits, no `git add`/commit, no writes to sibling
subdirectories by this aggregation step.
