# Modern libraries (3D / GUI / CLI) — aggregate verification

Branch `agent/modern-stdlib`. Aggregator: LIB agent 10/10.

This is an **aggregate** document. It only quotes sibling `RESULTS.md` files and
observed runner output; it never substitutes a build for an observed run and
never invents a number for an artifact that has not landed. Every missing
artifact is marked **PENDING** with the exact command that produces or verifies
it.

## 0. Honest scope (read this first)

- The evidence collected here is **fixed-seed, deterministic edge-case evidence
  produced on this host** (macOS, `/usr/bin/clang`, C+ driver). It is **not** a
  full safety proof.
- **Windows RUNTIME is BLOCKED.** Only PE compile/verify is in scope for the
  cross suite; no Windows runtime success is claimed.
- There is **no claim of Rust equivalence** and **no claim of 100% memory
  safety**.
- A `PASS` below means "the named runner exited 0 with its own documented
  checks"; it does not mean the library is universally correct.

## 1. Aggregate runner

Command (from the repository root):

```sh
sh tests/modern_libs_run_all.sh
```

Fixed environment inside the runner:

| Variable | Value |
|---|---|
| `PATH` | `/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:/usr/local/bin` |
| `CPLUS_CLANG` | `/usr/bin/clang` |
| `LC_ALL` | `C` |
| Per-runner bound | `timeout 900 sh <dir>/run.sh` |
| Logs | `build/modern_libs/<label>.log` |

Exit status: `0` = all nine runners PASS; `1` = at least one runner FAIL,
TIMEOUT or PENDING; `2` = setup error (`timeout` missing or log dir creation
failed). Execution continues after a failure so the table always covers all
nine suites. The runner uses only POSIX shell utilities and never swallows a
runner's non-zero status.

### Observed aggregate output

> State note: an early transcript captured during sibling editing showed a
> transient `mesh3d` FAIL caused by a root-path bug in that sibling's runner
> (`.../tests/build/c+`); the sibling corrected the root to `$DIR/../../..`.
> The transcript below is the later, corrected re-run.

```text
SUITE            RUNNER                                       STATUS       RC  LOG
---------------- -------------------------------------------- --------- -----  ---
math3d           tests/stdlib/math3d/run.sh                   PASS          0  build/modern_libs/math3d.log
scene3d          tests/stdlib/scene3d/run.sh                  PASS          0  build/modern_libs/scene3d.log
mesh3d           tests/stdlib/mesh3d/run.sh                   PASS          0  build/modern_libs/mesh3d.log
widgets          tests/stdlib/widgets/run.sh                  PASS          0  build/modern_libs/widgets.log
cli              tests/stdlib/cli/run.sh                      PASS          0  build/modern_libs/cli.log
demo_games3d     examples/games3d/run.sh                      PASS          0  build/modern_libs/demo_games3d.log
demo_gui_app     examples/gui_app/run.sh                      PENDING       -  MISSING (no log)
  PENDING command: timeout 900 sh examples/gui_app/run.sh
demo_cli_app     examples/cli_app/run.sh                      PASS          0  build/modern_libs/demo_cli_app.log
cross            tests/modern_libs_cross/run.sh               PENDING       -  MISSING (no log)
  PENDING command: timeout 900 sh tests/modern_libs_cross/run.sh
TOTALS           -                                            -             -  total=9 pass=7 fail=0 timeout=0 pending=2
TOTALS: total=9 pass=7 fail=0 timeout=0 pending=2
modern_libs aggregate: INCOMPLETE (PENDING counts as not verified)
EXIT=1
```

### 1.1 TOTALS at this capture

| Total suites | PASS | FAIL | TIMEOUT | PENDING |
|---:|---:|---:|---:|---:|
| 9 | 7 | 0 | 0 | 2 |

The two PENDING rows are `demo_gui_app` and `cross`; each is listed with its
exact command in §7.

## 2. Per-library results

API counts are the `total` column of the native coverage tool (§5), not a guess.
Case/passed/failed counts come from the sibling `RESULTS.md` for that library.

| Library | Header | APIs (coverage tool) | Cases | Passed | Failed | Runner | Evidence |
|---|---|---:|---:|---:|---:|---|---|
| math3d | `stdlib/math3d.hp` | 25 | 36 | 36 | 0 | PASS | `tests/stdlib/math3d/RESULTS.md` |
| scene3d | `stdlib/scene3d.hp` | 10 | 79 | 79 | 0 | PASS | `tests/stdlib/scene3d/RESULTS.md` |
| mesh3d | `stdlib/mesh3d.hp` | 19 | 37 | 37 | 0 | PASS | `tests/stdlib/mesh3d/RESULTS.md` |
| widgets | `stdlib/widgets.hp` | 0 reported / 20 declared (see §5) | 144 | 144 | 0 | PASS | `tests/stdlib/widgets/RESULTS.md`; `build/modern_libs/widgets.log` |
| cli | `stdlib/cli.hp` | 8 | 27 | 27 | 0 | PASS | `tests/stdlib/cli/RESULTS.md` |

**Library case totals: 323 cases / 323 passed / 0 failed** (36 + 79 + 37 +
144 + 27), all from the five sibling `RESULTS.md` files.

### 2.1 math3d

From `tests/stdlib/math3d/RESULTS.md`: **36 checks · 36 passed · 0 failed**;
build rc 0, run rc 0, exact 37-line stdout (36 `<tag> 1` lines plus
`MATH3D 36 0`), empty stderr. Covers vec3/mat4/quat determinism, NaN/Inf
refusal and division-by-zero guards.

### 2.2 cli

From `tests/stdlib/cli/RESULTS.md`: **27 checks · 27 passed · 0 failed**;
exact stdout diff PASS, empty stderr PASS; harness final line `CLI_TEST 27 27 0`.
Covers bounded parse/dispatch, refusals (duplicates, oversized inputs, null
pointers, capacity, malformed options) and deterministic help rendering.

### 2.3 scene3d

From `tests/stdlib/scene3d/RESULTS.md`: **79 checks · 79 passed · 0 failed**;
stderr empty, exit 0, full stdout transcript byte-for-byte verified. Covers
traversal order, refusal codes, allocation-failure points and canary probes.

### 2.4 mesh3d

From `tests/stdlib/mesh3d/RESULTS.md`: **37 checks · 0 failures · 0 malformed
lines**, program exit 0, stderr empty. Covers lifecycle/counts, bounding box
and center, normals, validation, refusals (NaN/Inf/index/overflow), atomicity
and custom-allocator paths.

### 2.5 widgets

From `tests/stdlib/widgets/RESULTS.md`: **144 checks · 144 passed · 0 failed**;
stdout byte-identical across two runs, stderr empty. Final harness line
`WIDGETS 144 0`. The runner log agrees:

```text
BUILD ok: .../build/c+ build test_widgets.cp --default --sanitize=none -I. -- build/libc/libcplus_hardened.a
RUN ok: WIDGETS 144 0 (stdout byte-identical across 2 runs, stderr empty)
WIDGETS SUITE PASS
```

Covers layout determinism, hit testing, exact serialization sizing and bounded
refusals with `error *` diagnostics. (An earlier intermediate snapshot showed
`WIDGETS 137 0`; the sibling expanded the suite to 144 before finalizing.)

## 3. Per-demo results

| Demo | Path | Status | Evidence |
|---|---|---|---|
| games3d | `examples/games3d/` | PASS | `build/modern_libs/demo_games3d.log` |
| gui_app | `examples/gui_app/` | PENDING | `examples/gui_app/run.sh` not present at capture time |
| cli_app | `examples/cli_app/` | PASS (host) / PE compile FAIL | `build/modern_libs/demo_cli_app.log` |

### 3.1 cli_app

`examples/cli_app/run.sh` exited 0 in the aggregate (runner rc 0). Its own log
reports:

```text
BUILD ok
PASS empty / add / mul / help / unknown_cmd / unknown_opt / too_many_args /
     too_long / empty_token / tag_no_value / verbose_eq / negative_token /
     bad_int / few_args / overflow / verbose / tag
CLI_APP_RESULT built=1 pass=17 fail=0 total=17 wincross=fail
```

**17 cases passed, 0 failed.** Honest caveat: the demo's Windows PE
cross-compile check **failed** (`MINGW COMPILEFAIL`: the generated temporary
`cplus-hp-*.h` header was missing when `stdlib/cli.hp` was included). The
runner still exits 0 because PE cross-compile is a compile-only bonus check;
this document records the PE failure explicitly and makes **no Windows runtime
claim**.

### 3.2 games3d

`examples/games3d/run.sh` exited 0. Its final line is
`SCENE3D 1 4 0 13` — 4 program checks passed, 0 failed, 13 refusal cases.
The runner additionally asserted: build ok; built-in run exit 0 with empty
stderr; exact stdout match against `expected/transcript.expected`; two runs
byte-identical; checksum line `CHECKSUM fnv1a32=b61bdf2b bytes=1779 lines=40`.
All 13 refusal probes returned the expected distinct error code (`E_NONFINITE`,
`E_INDEX`, `E_BOUNDS`, `E_CYCLE`, `E_PARSE`).

### 3.3 gui_app — PENDING

`examples/gui_app/` contained in-progress sources (`smoke.cp`, `smoke2.cp`)
but no runner at capture time, so no result is claimed.

Exact command to produce/verify the demo result:

```sh
sh examples/gui_app/run.sh
```

## 4. Cross-platform results (macOS + Windows PE + hostile)

**PENDING.** `tests/modern_libs_cross/RESULTS.md` had not landed at capture
time. No macOS, Windows PE or hostile-input count is asserted here; in
particular, **Windows runtime remains BLOCKED** and only PE compile/verify is
in scope.

Exact command to produce/verify the artifact:

```sh
sh tests/modern_libs_cross/run.sh   # expects tests/modern_libs_cross/RESULTS.md
```

## 5. Coverage cross-check

Cross-check command:

```sh
sh tests/stdlib/run_coverage.sh
```

### 5.1 Observed build caveat (honest)

With the default `c+` resolution (the checked-in `build/c+` wrapper), the
coverage tool did **not** build in this environment:

```text
error: native coverage tool build failed; see build/native-tools/coverage.build.log
tests/stdlib/coverage.cp:49:13: error: variable length array declaration not allowed at file scope
tests/stdlib/coverage.cp:51:13: error: variable length array declaration not allowed at file scope
2 errors generated.
```

The same script completed (exit `1` — see below) when pointed at the installed
driver explicitly:

```sh
CPLUS_BIN=/Users/haseeb-mir/.local/bin/c+ sh tests/stdlib/run_coverage.sh
```

No coverage source file was edited by this aggregator (strict file ownership).

### 5.2 Observed coverage rows for the new headers

```text
cli.hp                    0        8    0.0%
math3d.hp                 0       25    0.0%
mesh3d.hp                 0       19    0.0%
scene3d.hp                0       10    0.0%
```

- **Exact new-API number reported by the tool: 62** (`cli 8 + math3d 25 +
  mesh3d 19 + scene3d 10`). This is the live number from the tool, not a
  hard-coded `416`.
- **Exact total across all headers reported by the tool: 486.**
- `widgets.hp` contributes **0 reported APIs**: the tool's `is_skipped()`
  discards every name starting with `stdlib_`, and all 20 widget helpers use
  that prefix, so the header is omitted from the table entirely. The header
  itself declares **20** `static inline` public helpers (counted from
  `^static inline ...(` declarations in `stdlib/widgets.hp`); that count is a
  declaration count, not a tool-reported number, and is labelled as such.
- The `0.0%` figures are a **source-discovery limitation**, not evidence that
  the new libraries were never called. `gather_sources()` only scans
  `tests/stdlib/*.cp`, `multi_tu/`, `examples/`, `upstream_reuse/`,
  `allocator/` and `system_safety/support/`; the new suite sources live in
  nested directories (`tests/stdlib/<lib>/`) that are not scanned. The
  new-library suites have their own observed PASS results in §2.
- Exit `1` from the coverage script is expected while those four rows read
  `0.0%` (the tool's threshold is 80% per header).

## 6. Limitations / non-claims

1. Fixed-seed edge evidence on this host only; not a full safety proof.
2. Windows **runtime** is BLOCKED — PE compile/verify only.
3. No Rust equivalence claim; no 100% memory-safety claim.
4. Coverage is a static call-site heuristic with known discovery blind spots
   (§5), not execution coverage.
5. Runner PASS means the suite's own bounded checks passed; broad API
   correctness, platform portability and live-game behaviour are out of scope.
6. PENDING rows are unresolved artifacts, not failures and not passes; they
   make the aggregate exit nonzero because verification is incomplete.

## 7. PENDING artifact register

| Artifact | Exact command |
|---|---|
| `examples/gui_app/run.sh` + result | `sh examples/gui_app/run.sh` |
| `tests/modern_libs_cross/RESULTS.md` | `sh tests/modern_libs_cross/run.sh` |

## UPDATE — Windows runtime now verified on this host

Earlier notes in this document recorded Windows runtime as BLOCKED. That is
superseded: Highball/Wine engine `x64-crossover26.3-r27` now runs genuine PE32+
apps here when the two MinGW runtime DLLs (`libwinpthread-1.dll`,
`libgcc_s_seh-1.dll`) are beside the exe and a clean bottle state is used
(`highball run -v`). Eight real C+ apps were executed on both macOS arm64 and
Windows PE32+ with identical marker lines. Evidence, binaries, transcripts and
screenshots: `proof/real_apps_cross_platform/` (`CROSS_PROOF 58 58 0`). This is
host/engine-specific evidence, not a claim for all Windows configurations.
