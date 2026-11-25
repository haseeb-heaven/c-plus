# Modern libraries (3D / GUI / CLI) — aggregate verification

Branch `agent/modern-stdlib`. Aggregator: LIB agent 10/10.

This is an **aggregate** document. It only quotes sibling `RESULTS.md` files and
observed runner output; it never substitutes a build for an observed run and
never invents a number for an artifact that has not landed. Every missing
artifact is marked **PENDING** with the exact command that produces or verifies
it.

## Current verified apps (post-move — authoritative)

The three app suites formerly under `examples/{games3d,gui_app,cli_app}` are
now real applications in `apps/`; the old `examples/.../run.sh` entries are
compatibility shims only. Each verdict below is the owning runner's exact final
line, and each screenshot is rendered from that runner's actual stdout.

| App | Runner | Verified verdict | Screenshot | App README |
| --- | --- | --- | --- | --- |
| games3d — 3D scene app | `apps/games3d/run.sh` | `SCENE3D 1 4 0 13` | [games3d.svg](../../proof/all_apps_games_screenshots/screenshots/games3d.svg) | [apps/games3d](../../apps/games3d/README.md) |
| gui_app — GUI app | `apps/gui_app/run.sh` | `GUI_APP 30 6 0 24` | [gui_app.svg](../../proof/all_apps_games_screenshots/screenshots/gui_app.svg) | [apps/gui_app](../../apps/gui_app/README.md) |
| cli_app — calculator CLI | `apps/cli_app/run.sh` | `CLI_APP_RESULT built=1 pass=17 fail=0 total=17 wincross=ok` | [cli_app.svg](../../proof/all_apps_games_screenshots/screenshots/cli_app.svg) | [apps/cli_app](../../apps/cli_app/README.md) |

The historical aggregate transcript in §1 and the PENDING notes in §§3–7 below
are retained as a trace of the earlier capture; this section and the linked
app/proof pages supersede them. Windows PE32+ execution evidence is in
[proof/real_apps_cross_platform](../../proof/real_apps_cross_platform/README.md)
(`CROSS_PROOF 58 58 0`), host/engine-specific and not a portability guarantee.

## 0. Honest scope (read this first)

- The evidence collected here is **fixed-seed, deterministic edge-case evidence
  produced on this host** (macOS, `/usr/bin/clang`, C+ driver). It is **not** a
  full safety proof.
- **Windows runtime in the historical aggregate capture was BLOCKED** (PE
  compile/verify only). The current host/engine-specific Highball/Wine
  evidence is `CROSS_PROOF 58 58 0` (§4 and the "Current verified apps"
  section); no general Windows portability claim is made.
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
> The transcript below is the later, corrected re-run. Suite labels and
> runner paths are normalized to the current `apps/` locations; the original
> capture used pre-move `demo_*` labels under `examples/`.

```text
SUITE            RUNNER                                       STATUS       RC  LOG
---------------- -------------------------------------------- --------- -----  ---
math3d           tests/stdlib/math3d/run.sh                   PASS          0  build/modern_libs/math3d.log
scene3d          tests/stdlib/scene3d/run.sh                  PASS          0  build/modern_libs/scene3d.log
mesh3d           tests/stdlib/mesh3d/run.sh                   PASS          0  build/modern_libs/mesh3d.log
widgets          tests/stdlib/widgets/run.sh                  PASS          0  build/modern_libs/widgets.log
cli              tests/stdlib/cli/run.sh                      PASS          0  build/modern_libs/cli.log
games3d           apps/games3d/run.sh                         PASS          0  build/modern_libs/demo_games3d.log
gui_app           apps/gui_app/run.sh                         PENDING       -  MISSING (no log)
  PENDING command: timeout 900 sh apps/gui_app/run.sh
cli_app           apps/cli_app/run.sh                         PASS          0  build/modern_libs/demo_cli_app.log
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

The two historical PENDING rows are `gui_app` and `cross`; each is listed with
its exact command in §7. Both have since been verified — see "Current verified
apps" above and [proof/real_apps_cross_platform](../../proof/real_apps_cross_platform/README.md).

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

## 3. Per-app results (current; supersedes the historical capture)

These are real verified applications in `apps/`, not demos. The
`examples/{games3d,gui_app,cli_app}/run.sh` entries are compatibility shims.

| App | Runner | Verified verdict | Screenshot | Evidence |
|---|---|---|---|---|
| games3d | `apps/games3d/run.sh` | `SCENE3D 1 4 0 13` | [games3d.svg](../../proof/all_apps_games_screenshots/screenshots/games3d.svg) | [app README](../../apps/games3d/README.md) |
| gui_app | `apps/gui_app/run.sh` | `GUI_APP 30 6 0 24` | [gui_app.svg](../../proof/all_apps_games_screenshots/screenshots/gui_app.svg) | [app README](../../apps/gui_app/README.md) |
| cli_app | `apps/cli_app/run.sh` | `CLI_APP_RESULT built=1 pass=17 fail=0 total=17 wincross=ok` | [cli_app.svg](../../proof/all_apps_games_screenshots/screenshots/cli_app.svg) | [app README](../../apps/cli_app/README.md) |

### 3.1 cli_app

`apps/cli_app/run.sh` verifies 17 exact stdout/exit-code cases and finishes
with `CLI_APP_RESULT built=1 pass=17 fail=0 total=17 wincross=ok`. The earlier
capture recorded `wincross=fail` (`MINGW COMPILEFAIL`: a generated temporary
`cplus-hp-*.h` header was missing); the current runner's PE compile check
passes, and this runner still makes no Windows runtime claim. The separate
[cross-platform proof](../../proof/real_apps_cross_platform/README.md)
(`CROSS_PROOF 58 58 0`) does execute PE32+ builds under Highball/Wine.

### 3.2 games3d

`apps/games3d/run.sh` finishes with `SCENE3D 1 4 0 13` — 4 program checks
passed, 0 failed, 13 refusal cases. The runner asserts build ok; built-in run
exit 0 with empty stderr; exact stdout match against
`apps/games3d/expected/transcript.expected`; two byte-identical runs; and the
checksum `CHECKSUM fnv1a32=b61bdf2b bytes=1779 lines=40`. All 13 refusal probes
return the expected distinct error code (`E_NONFINITE`, `E_INDEX`, `E_BOUNDS`,
`E_CYCLE`, `E_PARSE`).

### 3.3 gui_app — verified

`apps/gui_app/run.sh` finishes with `GUI_APP 30 6 0 24` — 30 scenarios,
6 successes, 24 defined refusals, 0 failures. It builds the SDL3-backed app,
runs each case under `timeout 30` with `SDL_VIDEODRIVER=dummy`, and checks
byte-exact stdout (twice) with empty stderr. The earlier capture had no runner
at that time; that PENDING status is superseded here.

Reproduce all three:

```sh
sh apps/games3d/run.sh
sh apps/gui_app/run.sh
sh apps/cli_app/run.sh
```

## 4. Cross-platform results (macOS + Windows PE + hostile)

**Current:** the historical PENDING status is superseded by
[proof/real_apps_cross_platform](../../proof/real_apps_cross_platform/README.md),
which reports `CROSS_PROOF 58 58 0` for eight programs: genuine Mach-O arm64
and PE32+ x86-64 binaries, executed on this host (macOS arm64 and
Highball/Wine engine `x64-crossover26.3-r27`) with exact markers and exit 0.
The older compile-only campaign is recorded in
[tests/modern_libs_cross/RESULTS.md](../../tests/modern_libs_cross/RESULTS.md).
This remains host/engine-specific evidence, not a portability guarantee.

Reproduce:

```sh
sh proof/real_apps_cross_platform/run.sh
sh tests/modern_libs_cross/run.sh
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
2. Windows **runtime** was BLOCKED in the historical capture; the current
   host/engine-specific Highball/Wine evidence is `CROSS_PROOF 58 58 0` (§4).
   No general Windows portability claim is made.
3. No Rust equivalence claim; no 100% memory-safety claim.
4. Coverage is a static call-site heuristic with known discovery blind spots
   (§5), not execution coverage.
5. Runner PASS means the suite's own bounded checks passed; broad API
   correctness, platform portability and live-game behaviour are out of scope.
6. Historical PENDING rows were unresolved artifacts at capture time, not
   failures and not passes; they made that aggregate exit nonzero. Both are
   now verified (§§3–4).

## 7. Historical PENDING register (now resolved)

| Historical artifact | Current status |
|---|---|
| `apps/gui_app/run.sh` + result | Verified: `GUI_APP 30 6 0 24` ([app README](../../apps/gui_app/README.md)) |
| `tests/modern_libs_cross/RESULTS.md` | Verified: see §4 and [proof/real_apps_cross_platform](../../proof/real_apps_cross_platform/README.md) (`CROSS_PROOF 58 58 0`) |

## UPDATE — Windows runtime now verified on this host

Earlier notes in this document recorded Windows runtime as BLOCKED. That is
superseded: Highball/Wine engine `x64-crossover26.3-r27` now runs genuine PE32+
apps here when the two MinGW runtime DLLs (`libwinpthread-1.dll`,
`libgcc_s_seh-1.dll`) are beside the exe and a clean bottle state is used
(`highball run -v`). Eight real C+ apps were executed on both macOS arm64 and
Windows PE32+ with identical marker lines. Evidence, binaries, transcripts and
screenshots: `proof/real_apps_cross_platform/` (`CROSS_PROOF 58 58 0`). This is
host/engine-specific evidence, not a claim for all Windows configurations.
