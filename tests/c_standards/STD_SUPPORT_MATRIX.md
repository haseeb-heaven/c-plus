# C standard spelling × source type — support matrix

Generated 2026-11-23 (UTC) from the seven sibling group reports under
`tests/c_standards/<group>/RESULTS.md`. All evidence paths are relative to the
repository root. Verdict tokens:

| Token | Meaning |
|---|---|
| **PASS** | Spelling accepted; C+ build succeeded; binary ran with correct/byte-identical output. |
| **PARTIAL** | Spelling accepted and useful work succeeded, but a tested source/feature/mode failed. |
| **FAIL (source)** | Build failed for a genuine language reason in the source (reference compiler agrees), not a C+ defect. |
| **FAIL (spelling)** | Clang rejects the value (`invalid value '…' in '-std=…'`); no build possible. |
| **FAIL (safety)** | C+ default Safe-mode static-analysis rejection, identical across std values — no C-version signal. |
| **UNRECOGNIZED** | No such compiler flag; no published standard behind it. |
| **NOT TESTED** | No group exercised that spelling × source-type combination. |

"Minimal program" = a small, genuine source for that edition. "Real project" =
one or more of the unmodified `external_projects/{game_a,game_b,cli_app}/original/*.c`.
Unless stated otherwise, real-project success requires `--legacy-c` (the default
Safe mode rejects all three unmodified sources before `-std` matters).

---

## 1. Full cross-product

| Std spelling | Source type | Verdict | Exact evidence |
|---|---|---|---|
| `c89` | minimal (`hello89.c`) | **PASS** — build exit 0, stdout byte-identical to clang | `tests/c_standards/c89/run.log` §2 (A1/B1), §5; `tests/c_standards/c89/RESULTS.md` §2.2–3 |
| `c89` | real project (3 strict-C89 builds) | **FAIL (source)** — 0/3; clang `-std=c89 -pedantic-errors` also rejects all 3 (sources are C99-era) | `tests/c_standards/c89/run.log` §6; `tests/c_standards/real_projects/logs/results.tsv` (Matrix B `c89`: 1/3 — only 2048 builds) |
| `c90` | minimal (`hello89.c`) | **PASS** — build exit 0, stdout byte-identical | `tests/c_standards/c89/run.log` §2 (A2/B2), §5 |
| `c90` | real project | **FAIL (source)** — 0/3 strict; Matrix B 1/3 (2048 only) | `tests/c_standards/c89/run.log` §6; `tests/c_standards/real_projects/logs/results.tsv` (Matrix B `c90`) |
| `iso9899:1990` | minimal (`hello89.c`) | **PASS** — build exit 0, stdout byte-identical | `tests/c_standards/c89/run.log` §2 (A3/B3), §5 |
| `iso9899:1990` | real project | **FAIL (source)** — 0/3 strict; not in the 11-spelling cross-check matrix | `tests/c_standards/c89/run.log` §6 |
| `gnu89` | minimal (`hello89.c`) | **PASS** — build exit 0, stdout byte-identical (GNU-superset spelling, not strict ISO) | `tests/c_standards/c89/run.log` §2 (A4/B4), §5 |
| `gnu89` | real project | **PARTIAL** — Matrix B 1/3 builds (only 2048); snake/xdump fail on C99 `for(int i…)`; Matrix A 0/3 safety | `tests/c_standards/real_projects/logs/results.tsv` (Matrix B `gnu89`); `tests/c_standards/real_projects/RESULTS.md` §3 |
| `c94` | minimal (`c99_features.c`, `c94_features.c`) | **FAIL (spelling)** — `error: invalid value 'c94' in '-std=c94'` | `tests/c_standards/c99/logs/results.tsv` (rows `*_c94`); `tests/c_standards/c99/logs/ref_build_c99_features_c94.stderr` |
| `c94` | real project | **FAIL (spelling)** — invalid in clang, no C+ build attempted | `tests/c_standards/c99/RESULTS.md` §5.1; `tests/c_standards/real_projects/RESULTS.md` §2 |
| `iso9899:199409` | minimal (`c94_features.c`) | **PASS** — compiles/runs and matches expected stdout; `c99_features.c` rejected under strict reference but accepted by default C+ as extension → PARTIAL overall | `tests/c_standards/c99/logs/results.tsv`; `tests/c_standards/c99/logs/{ref,cp,cpped}_build_c99_features_iso9899_199409.stderr` |
| `iso9899:199409` | real project | **PARTIAL** — legacy mode 1/3 built (2048, 3/3 outputs matched); snake and xdump fail with `redefinition of 'i'`; Safe mode 0/3 | `tests/c_standards/c99/RESULTS.md` §5.2; `tests/c_standards/c99/logs/proj_{snake,xdump,2048}_iso9899_199409_legacy_build.stderr` |
| `c99` | minimal (`c99_features.c`, `c94_features.c`) | **PASS** — both build/run, stdout matches `expected/*.stdout` | `tests/c_standards/c99/logs/results.tsv` (rows `*_c99`); `tests/c_standards/c99/RESULTS.md` §3 |
| `c99` | real project | **PASS** — legacy 3/3 built and 7/7 golden outputs matched per project; Safe 0/3 | `tests/c_standards/c99/RESULTS.md` §4; `tests/c_standards/real_projects/logs/results.tsv` (Matrix B `c99`: 3/3) |
| `c9x` | minimal | **PASS** — same results as `c99` (clang alias) | `tests/c_standards/c99/RESULTS.md` §3, §6 |
| `c9x` | real project | **PASS** — legacy 3/3 built, outputs matched | `tests/c_standards/c99/RESULTS.md` §4; `tests/c_standards/real_projects/logs/results.tsv` (no `c9x` column in cross-check; c99 group supplies the evidence) |
| `gnu99` | minimal | **PASS** — same as `c99` (GNU superset) | `tests/c_standards/c99/RESULTS.md` §3, §6 |
| `gnu99` | real project | **PASS** — legacy 3/3 built, 7/7 outputs matched; cross-check Matrix B 3/3 | `tests/c_standards/c99/RESULTS.md` §4; `tests/c_standards/real_projects/logs/results.tsv` (Matrix B `gnu99`) |
| `c11` | minimal (`source.c`: atomics, `_Static_assert`, `_Generic`) | **PASS** — build 0, 0 diagnostics, runs → `int counter=7` | `tests/c_standards/c11/artifacts/run-report.log`; `tests/c_standards/c11/RESULTS.md` §2 |
| `c11` | real project | **PASS** — 3/3 built via `--legacy-c -std=c11`; 12/13 byte-identical (1 time-seeded 2048 stdin case differs) | `tests/c_standards/c11/RESULTS.md` §3; `tests/c_standards/c11/artifacts/game_a.run.stdout`, `game_b_*.run.stdout`, `cli_*.stdout` |
| `c1x` | minimal (`source.c`) | **PASS** — clang alias for C11; build 0, runs → `int counter=7` | `tests/c_standards/c11/RESULTS.md` §2 |
| `c1x` | real project | **NOT TESTED** — C11 group built real projects only with `-std=c11`; cross-check matrix has no `c1x` column | `tests/c_standards/c11/RESULTS.md` §3; `tests/c_standards/real_projects/RESULTS.md` §2 (11 accepted spellings omit `c1x`) |
| `gnu11` | minimal (`source.c`) | **PASS** — build 0, runs → `int counter=7` | `tests/c_standards/c11/RESULTS.md` §2 |
| `gnu11` | real project | **PASS** — cross-check Matrix B 3/3 built and matched; Matrix A 0/3 safety | `tests/c_standards/real_projects/logs/results.tsv` (Matrix B `gnu11`) |
| `c17` | minimal (`source.c`: `alignas`, `_Noreturn`, atomics) | **PASS** — clang 0 diagnostics, C+ 0 diagnostics, correct stdout | `tests/c_standards/c17/artifacts/results.tsv`; `tests/c_standards/c17/RESULTS.md` §4 |
| `c17` | real project | **PASS** — 3/3 built, 11/11 comparisons byte-identical | `tests/c_standards/c17/artifacts/comparisons.txt`; `tests/c_standards/c17/RESULTS.md` §5 |
| `c18` | minimal (`source.c`) | **PASS** — clang 0, C+ 0, correct stdout | `tests/c_standards/c17/artifacts/results.tsv` |
| `c18` | real project | **NOT TESTED** — C17 group used `-std=c17`; cross-check matrix has no `c18` column | `tests/c_standards/c17/RESULTS.md` §5; `tests/c_standards/real_projects/RESULTS.md` §2 |
| `iso9899:2017` | minimal (`source.c`) | **PASS** — clang 0, C+ 0, correct stdout | `tests/c_standards/c17/artifacts/results.tsv` |
| `iso9899:2017` | real project | **NOT TESTED** — no group built real projects with this spelling | `tests/c_standards/c17/RESULTS.md` §5 |
| `iso9899:2018` | minimal (`source.c`) | **PASS** — clang 0, C+ 0, correct stdout | `tests/c_standards/c17/artifacts/results.tsv` |
| `iso9899:2018` | real project | **NOT TESTED** — no group built real projects with this spelling | `tests/c_standards/c17/RESULTS.md` §5 |
| `gnu17` | minimal (`source.c`) | **PASS** — clang 0, C+ 0, correct stdout (GNU extensions, not strict ISO) | `tests/c_standards/c17/artifacts/results.tsv`; `tests/c_standards/c17/RESULTS.md` §4 note |
| `gnu17` | real project | **PASS** — cross-check Matrix B 3/3 built and matched | `tests/c_standards/real_projects/logs/results.tsv` (Matrix B `gnu17`) |
| `c23` | minimal (`source.c` / probes) | **FAIL (spelling)** — `error: invalid value 'c23' in '-std=c23'` on clang 16, forwarded verbatim by C+ | `tests/c_standards/latest/.work/probe/c23.err`; `tests/c_standards/c23/logs/`; `tests/c_standards/c23/RESULTS.md` §2 |
| `c23` | real project | **FAIL (spelling)** — 0/3 in `--legacy-c` with the same clang diagnostic; Safe mode also 0/3 | `tests/c_standards/c23/RESULTS.md` §3; `tests/c_standards/real_projects/RESULTS.md` §2 |
| `c2x` | minimal (C23 draft `source.c`) | **PASS** — C+ build+run, output `STDC=202000 mask=10 branch=elifdef-taken`; latest group's minimal output byte-identical to clang | `tests/c_standards/c23/logs/`; `tests/c_standards/latest/.work/run/minimal.tsv` |
| `c2x` | feature matrix (6 C23 features) | **PARTIAL** — `bool_true`, `binary_literal`, `typeof_operator` PASS; `nullptr` and `_BitInt` REJECTED by C+ `[CPLUS-FFI]`; digit separators HANG (rc=124, 10 s) | `tests/c_standards/latest/.work/run/feature_matrix.tsv`; `tests/c_standards/latest/RESULTS.md` §4; `tests/c_standards/c23/RESULTS.md` §4 |
| `c2x` | real project | **PASS (legacy)** — 3/3 built, 6/6 scenarios byte-identical to clang; Matrix B 3/3; Safe 0/3 | `tests/c_standards/c23/RESULTS.md` §3; `tests/c_standards/latest/.work/run/projects.tsv`; `tests/c_standards/real_projects/logs/results.tsv` |
| `gnu23` | minimal | **FAIL (spelling)** — `error: invalid value 'gnu23' in '-std=gnu23'` | `tests/c_standards/latest/.work/probe/gnu23.err`; `tests/c_standards/c23/RESULTS.md` §2 |
| `gnu23` | real project | **FAIL (spelling)** — 0/3 with same diagnostic | `tests/c_standards/c23/RESULTS.md` §3 |
| `gnu2x` | minimal (C23 draft) | **PASS** — same outcomes as `c2x`; stdout byte-identical to clang | `tests/c_standards/c23/RESULTS.md` §2; `tests/c_standards/latest/.work/run/minimal.tsv` |
| `gnu2x` | real project | **PASS (legacy)** — 3/3 built, 4/4 scenarios byte-identical; Matrix B 3/3; Safe 0/3 | `tests/c_standards/latest/.work/run/projects.tsv`; `tests/c_standards/real_projects/logs/results.tsv` |
| `c2y` | minimal | **UNRECOGNIZED** — draft-only, `error: invalid value 'c2y' in '-std=c2y'` | `tests/c_standards/latest/.work/probe/c2y.err`; `tests/c_standards/latest/RESULTS.md` §2.1 |
| `c2y` | real project | **UNRECOGNIZED** — no flag exists; not tested | `tests/c_standards/latest/RESULTS.md` §7 |
| `c26` | minimal | **UNRECOGNIZED** — draft-only, `error: invalid value 'c26' in '-std=c26'` | `tests/c_standards/latest/.work/probe/c26.err` |
| `c26` | real project | **UNRECOGNIZED** — no flag exists; not tested | `tests/c_standards/latest/RESULTS.md` §7 |
| `iso9899:2024` | minimal | **UNRECOGNIZED** — clang 16 aliases stop at 2018; `error: invalid value 'iso9899:2024' in '-std=iso9899:2024'` | `tests/c_standards/latest/.work/probe/iso9899:2024.err` |
| `iso9899:2024` | real project | **UNRECOGNIZED** — no flag exists; not tested | `tests/c_standards/latest/RESULTS.md` §7 |
| `gnu2y` (extra guess) | minimal | **UNRECOGNIZED** — rejected by clang | `tests/c_standards/latest/RESULTS.md` §2 |
| `gnu2y` (extra guess) | real project | **UNRECOGNIZED** — not tested | `tests/c_standards/latest/RESULTS.md` §2 |
| `c2y_draft` (extra guess) | minimal | **UNRECOGNIZED** — rejected by clang | `tests/c_standards/latest/RESULTS.md` §2 |
| `c2b` (extra guess) | minimal | **UNRECOGNIZED** — rejected by clang | `tests/c_standards/latest/RESULTS.md` §2 |
| `bogus` (control) | minimal | **FAIL (spelling)** — rejected by clang and forwarded verbatim; proves no silent normalization | `tests/c_standards/c17/artifacts/neg_bogus_cplus.stderr`; `tests/c_standards/c89/run.log` §7 |
| `"C21"` | any | **NOT APPLICABLE** — no published ISO C standard and no compiler flag by this name | `tests/c_standards/latest/RESULTS.md` §1, §7; `tests/c_standards/RESULTS.md` §5 |

---

## 2. Real-project cross-check summary (all accepted spellings)

Source: `tests/c_standards/real_projects/RESULTS.md` and
`tests/c_standards/real_projects/logs/results.tsv`. Matrix A = prescribed command
(`c+ build … -std=<val> --sanitize=none`, default Safe mode). Matrix B = same plus
`--legacy-c`.

| Std spelling | Matrix A (3 projects) | Matrix B (3 projects) | Per-cell evidence |
|---|---|---|---|
| `c89` | FAIL (safety) — 0/3 compiled | 1/3 compiled+matched (2048 only) | `logs/cells/A__game_a__c89.evidence.txt`, `logs/cells/B__game_a__c89.evidence.txt`, … |
| `c90` | FAIL (safety) | 1/3 (2048 only) | `logs/cells/B__game_b__c90.evidence.txt`, … |
| `c99` | FAIL (safety) | 3/3 | `logs/cells/B__game_a__c99.evidence.txt`, … |
| `c11` | FAIL (safety) | 3/3 | `logs/cells/B__game_b__c11.evidence.txt`, … |
| `c17` | FAIL (safety) | 3/3 | `logs/cells/B__cli_app__c17.evidence.txt`, … |
| `c2x` | FAIL (safety) | 3/3 | `logs/cells/B__game_a__c2x.evidence.txt`, … |
| `gnu89` | FAIL (safety) | 1/3 (2048 only) | `logs/cells/B__game_a__gnu89.evidence.txt`, … |
| `gnu99` | FAIL (safety) | 3/3 | `logs/cells/B__game_b__gnu99.evidence.txt`, … |
| `gnu11` | FAIL (safety) | 3/3 | `logs/cells/B__cli_app__gnu11.evidence.txt`, … |
| `gnu17` | FAIL (safety) | 3/3 | `logs/cells/B__game_a__gnu17.evidence.txt`, … |
| `gnu2x` | FAIL (safety) | 3/3 | `logs/cells/B__game_b__gnu2x.evidence.txt`, … |
| `c94`, `c23` | excluded — clang rejects the spelling | excluded — clang rejects the spelling | `tests/c_standards/real_projects/logs/clang_std_probe.txt` |

Totals: Matrix A 0/33 compiled, 0 matched, 33 failed; Matrix B 27/33 compiled,
27 ran, 27 matched, 6 failed (pre-C99 `redefinition of 'i'` in snake/xdump);
combined 27/66 compiled, 27 matched, 39 failed.

---

## 3. Pending groups

**None.** All seven expected groups landed `run.sh` and `RESULTS.md` before this
matrix was written, and the aggregate runner reported `0 pending`:

```
c_standards aggregate: 7 suites run, 7 passed, 0 failed, 0 pending
```

If a future group is added under `tests/c_standards/<name>/` without a `run.sh`,
`run_all.sh` prints it as `PENDING` together with the exact reproduction command
`sh tests/c_standards/<name>/run.sh`. No group is currently in that state.
