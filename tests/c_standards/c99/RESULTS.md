# C94 / C99 standard-spelling support — C+ compiler driver

Evidence report.  All commands below were executed on this machine and their
exact argv, exit code, stdout, and stderr are preserved under
`tests/c_standards/c99/logs/` (400 files; `results.tsv` has 124 rows).
No result was inferred from a build alone, and no failure was hidden.

Generated: 2026-11-23T06:56:34Z (re-run of `run.sh` after adding trailing
newlines to the two sources).

## 0. Environment and mechanism

* Compiler: `/usr/bin/clang` → `Apple clang version 16.0.0 (clang-1600.0.26.6)`,
  target `arm64-apple-darwin27.2.0`.
* Driver under test: `./build/c+` (repo branch `agent/modern-stdlib`).
* `timeout` is `/usr/bin/timeout` (BSD-style `timeout <duration> <command>`).
* Mechanism (source read + verbose probe, `logs/driver_verbose.txt`):
  `compiler/Main.cpp:196-203` forwards any `-std=...` token into
  `extraArgs`; `compiler/Sema/SemaCPlus.cpp:24` / `compiler/Driver/Driver.cpp:489`
  place a default (`-std=c17` / `-std=gnu17`) first and the user token last, so
  **the user spelling reaches Clang verbatim and wins**. The verbose probe shows
  `-std=gnu17` (default) followed by `-std=c99` (user) in the driver's backend
  argument list and `-std=c99` in the actual `clang -cc1` invocation.
  Consequence: unsupported spellings are rejected by Clang, not normalized by
  the C+ driver.
* The driver does **not** pass `-pedantic-errors` by default.  The required
  command form in this task (`c+ build <file> -std=<val> --sanitize=none -o <bin>`)
  therefore permits C99 extensions as warnings in C94 mode.  A separate
  `+pedantic` probe (extra `-pedantic-errors`) was added to expose this.

## 1. Sources created (all under `tests/c_standards/c99/`)

| File | What it genuinely exercises |
|---|---|
| `c99_features.c` | `//` comments, declaration after statement (`int sum` after `printf`), declaration in `for` init, `<stdint.h>` fixed-width types (`uint32_t`, `int64_t`), `long long`. Expected stdout: 3 lines (`C99 u=deadbeef`, `C99 sum=6`, `C99 big=-1234567890123`), stored byte-exactly in `expected/c99_features.stdout`. |
| `c94_features.c` | ISO 9899:1990/Amendment-1 digraphs (`<%` `%>` `<:` `:>`), `wchar_t`, `L'A'`, `<wchar.h>`; deliberately no C99 constructs. Expected stdout: 2 lines (`C94 wc=65 a0=1 a2=3`, `C94 sum=9`), stored in `expected/c94_features.stdout`. |

Both files compile cleanly with plain Clang in their intended mode:
`/usr/bin/clang -std=c99 -pedantic-errors c99_features.c` → exit 0;
`/usr/bin/clang -std=iso9899:199409 -pedantic-errors c94_features.c` → exit 0.

`expected/` also contains byte-exact outputs copied from the real projects'
recorded original runs (`snake.stdout`, `2048_{test,version,help}.stdout`,
`xdump_{version,help,file_default}.stdout`), verified with `cmp` against
`external_projects/*/ORIGINAL_EVIDENCE.md` capture files before use.

## 2. Exact command template (every spelling)

Reference (per source `S`, spelling `V`):

```
/usr/bin/clang -std=V -pedantic-errors tests/c_standards/c99/S.c -o tests/c_standards/c99/.work/S_V_ref
```

Driver, exact required form:

```
./build/c+ build tests/c_standards/c99/S.c -std=V --sanitize=none -o tests/c_standards/c99/.work/S_V_cp
```

Extra parity probe:

```
./build/c+ build tests/c_standards/c99/S.c -std=V -pedantic-errors --sanitize=none -o tests/c_standards/c99/.work/S_V_cpped
```

Every case has `logs/<case>.cmd` (exact `%q`-quoted argv), `.stdout`, `.stderr`,
`.exit`.  Successful builds were run and stdout compared with `cmp -s` against
the matching `expected/*.stdout`.

## 3. Minimal-source matrix (exit codes)

`ref` = plain clang + `-pedantic-errors`; `c+` = required driver form;
`c+ped` = driver form + `-pedantic-errors`.  `0` = compile succeeded and the
binary ran with exit 0 and byte-identical expected stdout; `1` = compile
failed; `SKIPPED` = no binary to run.

| Source | std spelling | ref | c+ | c+ped | evidence |
|---|---|---|---|---|---|
| c99_features | `c94` | 1 | 1 | 1 | `logs/{ref,cp,cpped}_build_c99_features_c94.stderr` |
| c99_features | `iso9899:199409` | 1 | 0 | 1 | `logs/{ref,cp,cpped}_build_c99_features_iso9899_199409.*` |
| c99_features | `c99` | 0 | 0 | 1* | `logs/{ref,cp,cpped}_build_c99_features_c99.*` |
| c99_features | `c9x` | 0 | 0 | 1* | `logs/cpped_build_c99_features_c9x.stderr` |
| c99_features | `gnu99` | 0 | 0 | 1* | `logs/cpped_build_c99_features_gnu99.stderr` |
| c94_features | `c94` | 1 | 1 | 1 | `logs/{ref,cp,cpped}_build_c94_features_c94.stderr` |
| c94_features | `iso9899:199409` | 0 | 0 | 1* | `logs/cpped_build_c94_features_iso9899_199409.stderr` |
| c94_features | `c99` | 0 | 0 | 1* | `logs/cpped_build_c94_features_c99.stderr` |
| c94_features | `c9x` | 0 | 0 | 1* | `logs/cpped_build_c94_features_c9x.stderr` |
| c94_features | `gnu99` | 0 | 0 | 1* | `logs/cpped_build_c94_features_gnu99.stderr` |

`*` = the only c+ped failure is the C+ rewriter's missing final newline
(`error: no newline at end of file [-Werror,-Wnewline-eof]`), not a standard
violation; see §6.  For `iso9899:199409` + `c99_features`, c+ped additionally
reproduces all strict-ISO errors (see §5.2).

## 4. Real-project matrix (C+ driver)

Projects, compiled **unmodified** from `external_projects/*/original/`:

| Project | Source | Deterministic run (expected output) |
|---|---|---|
| game_a snake | `game_a/original/snake.c` | `printf 'q' > q.in`; `timeout 30 <bin> < q.in` → 44 bytes ending `Game over! Score: 0\n` |
| game_b 2048 | `game_b/original/2048.c` | `timeout 30 <bin> test` → `All 13 tests executed successfully`; `--version`, `--help` |
| cli_app xdump | `cli_app/original/xdump.c` | `timeout 30 <bin> -v`, `-h`, and `timeout 30 <bin> sample.bin` (52-byte deterministic fixture) |

Two modes were built for every spelling:

* `safe` = exactly the required command form (driver defaults to Safe mode).
* `legacy` = same command plus `--legacy-c` (the unmodified upstream sources are
  not C+ memory-safe; see §5.3).  This does **not** change the Clang `-std=`
  token — it only removes the C+ safety analysis (`-fcplus-legacy`).

`M` in the table = run exit 0 **and** stdout byte-identical to the recorded
original output.  `—` = build failed, no run attempted.

| std spelling | mode | snake | 2048 (test/version/help) | xdump (v/h/file) |
|---|---|---|---|---|
| `c99` | safe | build 1 | build 1 | build 1 |
| `c99` | legacy | build 0; run M | build 0; M/M/M | build 0; M/M/M |
| `c9x` | safe | build 1 | build 1 | build 1 |
| `c9x` | legacy | build 0; run M | build 0; M/M/M | build 0; M/M/M |
| `gnu99` | safe | build 1 | build 1 | build 1 |
| `gnu99` | legacy | build 0; run M | build 0; M/M/M | build 0; M/M/M |
| `iso9899:199409` | safe | build 1 | build 1 | build 1 |
| `iso9899:199409` | legacy | build 1 (`redefinition of 'i'`) | build 0; M/M/M | build 1 (`redefinition of 'i'`) |
| `c94` | safe | build 1 | build 1 | build 1 |
| `c94` | legacy | build 1 (invalid spelling) | build 1 (invalid spelling) | build 1 (invalid spelling) |

Zero `MISMATCH` results were recorded: every successful run matched its
original output byte-for-byte; every failure is documented below.

Representative exact commands/evidence (full set in logs):

```
./build/c+ build external_projects/game_a/original/snake.c -std=c99 --sanitize=none -o tests/c_standards/c99/.work/snake_c99_legacy   # + --legacy-c; exit 0
/usr/bin/timeout 30 tests/c_standards/c99/.work/snake_c99_legacy < .work/q.in                                                          # exit 0, MATCH
./build/c+ build external_projects/game_b/original/2048.c -std=c99 --sanitize=none --legacy-c -o .work/2048_c99_legacy               # exit 0
/usr/bin/timeout 30 .work/2048_c99_legacy test                                                                                          # exit 0, MATCH
./build/c+ build external_projects/cli_app/original/xdump.c -std=c99 --sanitize=none --legacy-c -o .work/xdump_c99_legacy            # exit 0
/usr/bin/timeout 30 .work/xdump_c99_legacy .work/sample.bin                                                                            # exit 0, MATCH
```

(Log commands use the full `tests/c_standards/c99/` relative prefix; paths
above are shortened only in this prose.)

## 5. Documented failures / divergences

### 5.1 `c94` is not a valid Clang spelling — UNSUPPORTED

Both reference and driver emit the identical diagnostic and exit 1:

```
error: invalid value 'c94' in '-std=c94'
note: use 'iso9899:199409' for 'ISO C 1990 with amendment 1' standard
...
```

Evidence: `logs/ref_build_c99_features_c94.stderr` and
`logs/cp_build_c99_features_c94.stderr`.  The C+ driver does not alias `c94` to
`iso9899:199409`; it forwards the token.  All 12 `c94` build cases (2 sources x
ref/c+/c+ped = 6, plus 3 projects x safe/legacy = 6) fail for this reason.

### 5.2 `iso9899:199409` is accepted as a spelling but is not C99 — PARTIAL

Reference (`-pedantic-errors`) correctly rejects `c99_features.c`, exit 1, all
5 strict-ISO errors:

```
error: // comments are not allowed in this language [-Werror,-Wcomment]
error: variable declaration in for loop is a C99-specific feature [-Werror,-Wc99-extensions]
error: 'long long' is an extension when C99 mode is not enabled [-Werror,-Wlong-long]   (x2)
error: mixing declarations and code is a C99 extension [-Werror,-Wdeclaration-after-statement]
1 warning and 5 errors generated.
```

(`logs/ref_build_c99_features_iso9899_199409.stderr`)

The required driver form (no `-pedantic-errors`) **accepts** the same C99 source
with only a warning, exit 0, and the output still matches:

```
warning: GCC does not allow variable declarations in for loop initializers before C99 [-Wgcc-compat]
1 warning generated.
```

(`logs/cp_build_c99_features_iso9899_199409.stderr`).  This is a real
strictness divergence: C94 mode through C+ by default behaves as "C94 plus
tolerated C99 extensions", whereas plain clang with `-pedantic-errors` rejects
them.  Adding `-pedantic-errors` to the driver restores the rejection
(`logs/cpped_build_c99_features_iso9899_199409.stderr`, 6 errors).

Genuine C94 code works: `c94_features.c` compiles and matches under both
reference and driver (`logs/{ref,cp}_build_c94_features_iso9899_199409.*`).

Real-project consequences in `iso9899:199409` legacy mode:
snake and xdump fail with actual C94-scope errors (`redefinition of 'i'` from
repeated `for (int i = ...)` / `for (long i = ...)` declarations), see
`logs/proj_snake_iso9899_199409_legacy_build.stderr` and
`logs/proj_xdump_iso9899_199409_legacy_build.stderr`.  2048 happens to avoid
that construct, so it builds and all three outputs match
(`logs/proj_2048_iso9899_199409_legacy_*`).  Therefore this spelling is
PARTIAL: usable for C94-conformant sources, not equivalent to C99.

### 5.3 Default (Safe) C+ mode rejects all three real projects — independent of std

Every `safe` build fails for every spelling, including `c99`, with C+ safety
diagnostics rather than ISO C diagnostics, e.g.:

* snake, 48 stderr lines: `[CPLUS-BOUNDS] negative array index on 'sx'`,
  `[CPLUS-LIFETIME] variable 'c' is uninitialized (read before it is
  initialized)` — `logs/proj_snake_c99_safe_build.stderr`.
* 2048, 63 stderr lines: `[CPLUS-FFI] 'printf' is not async-signal-safe`,
  `[CPLUS-LIFETIME] variable 'fg' is uninitialized` —
  `logs/proj_2048_c99_safe_build.stderr`.
* xdump, 23 stderr lines: `[CPLUS-OWNER] variable-length stack allocation has
  no bounds check; use cplus_alloc` — `logs/proj_xdump_c99_safe_build.stderr`.

These are safety-analysis results on unmodified upstream C, not evidence that
`-std=c99` is broken.  With `--legacy-c` the same sources and same `-std`
tokens compile and produce byte-identical original outputs (§4).  The minimal
C99/C94 sources, which contain no such safety hazards, build in default Safe
mode too.  This caveat is recorded rather than counted as a std failure.

### 5.4 C+ rewriter drops the final newline (extra finding)

`logs/cpped_build_*_c99.stderr` (and every other valid-spelling c+ped case)
shows the single error `no newline at end of file [-Werror,-Wnewline-eof]`.
Investigation with `--emit-c` confirms the original files end in `0a` while the
C+-lowered `.c` ends in `}` (`0x7d`).  This makes the *extra* `-pedantic-errors`
probe fail even for valid C99 code.  The required driver command form is
unaffected; this is noted as a genuine driver defect, not a fabricated pass.

## 6. Classification (final)

| Spelling | Classification | Exact evidence |
|---|---|---|
| `c94` | **UNSUPPORTED** | Clang 16 rejects the value (`error: invalid value 'c94'`); C+ forwards it verbatim; 0/2 sources and 0/3 real projects compile. |
| `iso9899:199409` | **PARTIAL** | Valid spelling. C94-conformant source compiles/runs and matches (ref + c+). C99-only source correctly rejected under strict ISO; accepted by default c+ as extension. Real projects: 2048 builds + 3/3 outputs match, snake and xdump fail with `redefinition of 'i'`. |
| `c99` | **SUPPORTED** | Both minimal sources compile/run and match (ref + c+). All 3 real projects compile via c+ legacy mode and 7/7 deterministic outputs match (`snake q`, `2048 test/--version/--help`, `xdump -v/-h/file`). Safe-mode rejections are C+ safety analysis, not std. |
| `c9x` | **SUPPORTED** | Same results as `c99` in every case (accepted Clang alias); 7/7 real-project outputs match. |
| `gnu99` | **SUPPORTED** | Same results as `c99` in every case (GNU-superset of C99); 7/7 real-project outputs match. |

## 7. Artifacts and reproduction

```
tests/c_standards/c99/
├── sources:      c99_features.c, c94_features.c
├── expected/:    8 byte-exact expected stdout files
├── run.sh:       deterministic evidence collector (re-runnable, exits 0)
├── RESULTS.md:   this report
├── logs/:        SUMMARY.txt, results.tsv, console.log, driver_verbose.txt,
│                 and <case>.{cmd,stdout,stderr,exit} for every compile/run
└── .work/:       generated binaries, stdin fixture, sample.bin, lowered C
```

Reproduce: `bash tests/c_standards/c99/run.sh` (from any directory).  It
recreates `.work/` and `logs/` and never writes to `external_projects/` or any
source file.  No `git add`/commit was performed, and no files outside
`tests/c_standards/c99/` were created or modified by this task.
