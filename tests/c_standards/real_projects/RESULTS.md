# Real downloaded C projects × C standard spellings — C+ compiler matrix

Subagent 7/10. Branch `agent/modern-stdlib`. All generated artifacts live under
`tests/c_standards/real_projects/logs/` (gitignored via the repository's `logs/`
rule). Nothing outside `tests/c_standards/real_projects/` was created or modified;
no source was edited; no `git add`/commit was performed.

## 1. Scope and sources (unmodified, hash-verified)

Three real, permissively licensed (MIT), already-downloaded single-file C projects
were compiled **as-is**. Their `ORIGINAL_EVIDENCE.md` files record plain-clang
build/run results and the golden outputs used for comparison here.

| id | source | SHA-256 (verified before and after) | golden evidence |
|---|---|---|---|
| `game_a` | `external_projects/game_a/original/snake.c` | `13de0d13e1ee1e621c48d7f91ac283126a04c3980b0c22c4fafbf81d25bc5774` | `external_projects/game_a/ORIGINAL_EVIDENCE.md`, `build/run.stdout` |
| `game_b` | `external_projects/game_b/original/2048.c` | `90f794c675a696aa10980dd120d1f0503ee4ed33a61fef92c7df8f443f5b18a8` | `external_projects/game_b/ORIGINAL_EVIDENCE.md`, `run_*_std*.log` |
| `cli_app` | `external_projects/cli_app/original/xdump.c` | `93106c3b4b7bb4f0608bd3308621eca30d8ab7c4c7883b23f54d3c7fe0309253` | `external_projects/cli_app/ORIGINAL_EVIDENCE.md`, `build/runs/*` |

## 2. Which `-std` spellings clang accepts (probed, not assumed)

Probe: `printf 'int main(void){return 0;}\n' | /usr/bin/clang -x c -std=<val> - -o /dev/null`
with `/usr/bin/clang` = Apple clang 16.0.0 (`clang-1600.0.26.6`). Full log:
`logs/clang_std_probe.txt`.

| spelling | result |
|---|---|
| c89 | ACCEPTED |
| c90 | ACCEPTED |
| c94 | REJECTED — `error: invalid value 'c94' in '-std=c94'` |
| c99 | ACCEPTED |
| c11 | ACCEPTED |
| c17 | ACCEPTED |
| c23 | REJECTED — `error: invalid value 'c23' in '-std=c23'` |
| c2x | ACCEPTED |
| gnu89 | ACCEPTED |
| gnu99 | ACCEPTED |
| gnu11 | ACCEPTED |
| gnu17 | ACCEPTED |
| gnu2x | ACCEPTED |

**11 accepted spellings** form the matrix columns. `c94` and `c23` are excluded
because the local clang rejects them (recorded above); no C+ build was attempted
for those two.

## 3. Method

Two matrices were run. Every cell is a real `./build/c+` invocation from the repo
root; a compile success is followed by real `timeout 30` runs compared against the
preserved golden outputs. Cell statuses:

- `COMPILE_FAIL` — C+ driver exited non-zero (or produced no executable). Exact
  driver stdout/stderr is in the cell evidence file.
- `RUN_MATCH` — compile succeeded and **all** fixed golden run cases matched
  (byte-exact stdout/stderr and exit code; see §6 for the 2048 stdin exception).
- `RUN_DIFFERENT` — compile succeeded but at least one golden case differed.

No `RUN_DIFFERENT` cells occurred.

### Matrix A — exact command required by the task (primary)

```
./build/c+ build <file> -std=<val> --sanitize=none -o <bin>
```

This is C+ **default mode** ("report proven errors, warn if unverified"). It
rejects all three unmodified real C sources before any C-version behavior can be
observed, because their safety checker findings are hard errors here. The driver
stderr is **byte-identical for every `-std` value within each project** (verified
by SHA-256: `game_a` `c77ee69f…`, `game_b` `c17eb46a…`, `cli_app`
`0d818614…`), so this matrix carries no C-version signal — it is reported
faithfully as the task-prescribed result, not hidden.

| project | c89 | c90 | c99 | c11 | c17 | c2x | gnu89 | gnu99 | gnu11 | gnu17 | gnu2x |
|---|---|---|---|---|---|---|---|---|---|---|---|
| game_a | COMPILE_FAIL | COMPILE_FAIL | COMPILE_FAIL | COMPILE_FAIL | COMPILE_FAIL | COMPILE_FAIL | COMPILE_FAIL | COMPILE_FAIL | COMPILE_FAIL | COMPILE_FAIL | COMPILE_FAIL |
| game_b | COMPILE_FAIL | COMPILE_FAIL | COMPILE_FAIL | COMPILE_FAIL | COMPILE_FAIL | COMPILE_FAIL | COMPILE_FAIL | COMPILE_FAIL | COMPILE_FAIL | COMPILE_FAIL | COMPILE_FAIL |
| cli_app | COMPILE_FAIL | COMPILE_FAIL | COMPILE_FAIL | COMPILE_FAIL | COMPILE_FAIL | COMPILE_FAIL | COMPILE_FAIL | COMPILE_FAIL | COMPILE_FAIL | COMPILE_FAIL | COMPILE_FAIL |

Per-std totals (compiled / ran / matched / failed, across the 3 projects):

| std | compiled | ran | matched | failed |
|---|---:|---:|---:|---:|
| c89 | 0 | 0 | 0 | 3 |
| c90 | 0 | 0 | 0 | 3 |
| c99 | 0 | 0 | 0 | 3 |
| c11 | 0 | 0 | 0 | 3 |
| c17 | 0 | 0 | 0 | 3 |
| c2x | 0 | 0 | 0 | 3 |
| gnu89 | 0 | 0 | 0 | 3 |
| gnu99 | 0 | 0 | 0 | 3 |
| gnu11 | 0 | 0 | 0 | 3 |
| gnu17 | 0 | 0 | 0 | 3 |
| gnu2x | 0 | 0 | 0 | 3 |

**Matrix A grand total: compiled 0 / 33, ran 0, matched 0, failed 33.**

Representative exact C+ driver diagnostics (verbatim from the per-cell evidence;
these repeat identically for every std):

```
# game_a (c99)
snake.c:44:39: error: [CPLUS-BOUNDS] [CPLUS-SAFE-BOUNDS] negative array index on 'sx'
snake.c:44:54: error: [CPLUS-BOUNDS] [CPLUS-SAFE-BOUNDS] negative array index on 'sy'
snake.c:31:16: error: [CPLUS-LIFETIME] variable 'c' is uninitialized (read before it is initialized)
... (16 errors total; full text in logs/cells/A__game_a__c99.evidence.txt)

# game_b (c99)
2048.c:433:2: error: [CPLUS-FFI] 'printf' is not async-signal-safe
2048.c:436:2: error: [CPLUS-FFI] 'printf' is not async-signal-safe
2048.c:61:36: error: [CPLUS-LIFETIME] variable 'fg' is uninitialized (read before it is initialized)
... (8 errors total; full text in logs/cells/A__game_b__c99.evidence.txt)

# cli_app (c99)
xdump.c:62:26: error: [CPLUS-OWNER] variable-length stack allocation has no bounds check; use cplus_alloc
xdump.c:184:29: error: [CPLUS-LIFETIME] variable 'endptr' is uninitialized (read before it is initialized)
xdump.c:238:29: error: [CPLUS-LIFETIME] variable 'endptr' is uninitialized (read before it is initialized)
(full text in logs/cells/A__cli_app__c99.evidence.txt)
```

### Matrix B — supplementary, same command plus `--legacy-c`

`--legacy-c` is the driver's documented "legacy C compatibility mode", which lets
the **unmodified** real C sources through the default-mode safety gate so that the
C-standard behavior becomes observable. This matrix is supplementary to Matrix A;
it is *not* a replacement for the prescribed command.

```
./build/c+ build <file> -std=<val> --sanitize=none --legacy-c -o <bin>
```

| project | c89 | c90 | c99 | c11 | c17 | c2x | gnu89 | gnu99 | gnu11 | gnu17 | gnu2x |
|---|---|---|---|---|---|---|---|---|---|---|---|
| game_a | COMPILE_FAIL | COMPILE_FAIL | RUN_MATCH | RUN_MATCH | RUN_MATCH | RUN_MATCH | COMPILE_FAIL | RUN_MATCH | RUN_MATCH | RUN_MATCH | RUN_MATCH |
| game_b | RUN_MATCH | RUN_MATCH | RUN_MATCH | RUN_MATCH | RUN_MATCH | RUN_MATCH | RUN_MATCH | RUN_MATCH | RUN_MATCH | RUN_MATCH | RUN_MATCH |
| cli_app | COMPILE_FAIL | COMPILE_FAIL | RUN_MATCH | RUN_MATCH | RUN_MATCH | RUN_MATCH | COMPILE_FAIL | RUN_MATCH | RUN_MATCH | RUN_MATCH | RUN_MATCH |

Per-std totals:

| std | compiled | ran | matched | failed |
|---|---:|---:|---:|---:|
| c89 | 1 | 1 | 1 | 2 |
| c90 | 1 | 1 | 1 | 2 |
| c99 | 3 | 3 | 3 | 0 |
| c11 | 3 | 3 | 3 | 0 |
| c17 | 3 | 3 | 3 | 0 |
| c2x | 3 | 3 | 3 | 0 |
| gnu89 | 1 | 1 | 1 | 2 |
| gnu99 | 3 | 3 | 3 | 0 |
| gnu11 | 3 | 3 | 3 | 0 |
| gnu17 | 3 | 3 | 3 | 0 |
| gnu2x | 3 | 3 | 3 | 0 |

**Matrix B grand total: compiled 27 / 33, ran 27, matched 27, failed 6.**

The 6 failures are all pre-C99 modes for sources that use C99 for-loop
declaration scoping — a genuine C-version effect, not a C+ safety diagnostic:

- `game_a` (`snake.c`) at `c89`, `c90`, `gnu89`:
  `error: redefinition of 'i'` at lines 42/44 — three `for(int i=...)` loops in
  one scope, illegal before C99 (preceded by
  `warning: GCC does not allow variable declarations in for loop initializers before C99`).
- `cli_app` (`xdump.c`) at `c89`, `c90`, `gnu89`: same pattern for `long i`
  at lines 113/140 (`error: redefinition of 'i'`).
- `game_b` (`2048.c`) declares its loop counters outside the loops, so it builds
  and matches in all 11 modes, including c89/c90/gnu89.

## 4. Totals summary

| matrix | cells | compiled | ran | matched (RUN_MATCH) | failed (COMPILE_FAIL + RUN_DIFFERENT) | RUN_DIFFERENT |
|---|---:|---:|---:|---:|---:|---:|
| A (exact prescribed command) | 33 | 0 | 0 | 0 | 33 | 0 |
| B (supplementary `--legacy-c`) | 33 | 27 | 27 | 27 | 6 | 0 |
| combined | 66 | 27 | 27 | 27 | 39 | 0 |

## 5. Run cases and golden comparison

All runs used `timeout 30`. Golden stdout/stderr/exit came from the files named
in §1 (recorded by the original plain-clang builds).

- `game_a`: `printf 'q' | <bin>` → 44-byte stdout
  (`ESC[2J ESC[?25l ESC[?25h ESC[2J ESC[H Game over! Score: 0\n`), exit 0, empty stderr.
- `game_b`: `<bin> test` (35 B, `All 13 tests executed successfully`),
  `<bin> --version` (21 B), `<bin> --help` (338 B), and `printf 'qy' | <bin>`.
  The three deterministic cases are byte-exact. The stdin case's board is seeded
  from `time(NULL)`, so byte-exact comparison is invalid there; instead fixed
  control-flow markers were checked (see §6).
- `cli_app`: `<bin> -v` (20 B), `-h` (433 B), `<fixture>` (313 B),
  `-w 8 -g 4 <fixture>` (334 B), `-s 32 -n 16 <fixture>` (88 B), and
  `<fixture>` on stdin (313 B). Fixture:
  `external_projects/cli_app/build/fixtures/sample.bin`. All byte-exact.
  (`-Z` / missing-file cases were omitted from the cell verdicts because their
  stderr embeds `argv[0]`, which changes with the generated binary path; they do
  not affect the C-version question.)

## 6. Honesty notes

- Every `RUN_MATCH` above means a binary actually produced by the C+ driver was
  actually executed and compared. No pass was inferred from a build or from the
  original evidence alone.
- The 2048 stdin case is the one non-byte-exact comparison: it was accepted only
  when exit=0, stderr empty, stdout contained the `QUIT? (y/n)` prompt, the
  `←,↑,→,↓ or q` legend and the `ESC[?25h ESC[m` terminal-restore sequence, and
  did **not** contain `Error! Cannot read keyboard input!` (clean quit).
- The `c89`/`c90`/`gnu89` failures in Matrix B are source-language incompatibilities
  in the downloaded originals (C99 for-loop declarations), reported as-is; the
  sources were not patched to make them pass.
- Re-running `run.sh` produced a byte-identical `results.tsv` (checked).

## 7. What this does and does not show

**This shows which C versions the C+ compiler handles for these real sources:**
under the task-prescribed default-mode command, none of the three unmodified
real sources compile at any `-std` value because the default-mode safety gate
rejects them first. In the documented `--legacy-c` compatibility mode, the C+
backend accepts and produces byte-for-byte output-matching executables for the
C99-and-later spellings (`c99`, `c11`, `c17`, `c2x`, `gnu99`, `gnu11`, `gnu17`,
`gnu2x`) for all three sources; for the pre-C99 spellings (`c89`, `c90`, `gnu89`)
only the source that avoids C99 for-loop declarations (`game_b`) works, while
`game_a` and `cli_app` fail on genuine pre-C99 language violations.

**It does NOT prove support for every C feature or platform.** Only three
single-file sources, one clang version (Apple clang 16.0.0) on macOS/arm64, and
one fixed set of inputs per project were exercised. Passing these files says
nothing about untested language constructs, other compilers/versions, other
operating systems/architectures, library or ABI completeness, or the C+ safe-mode
semantics deliberately bypassed by `--legacy-c`.

## 8. Reproduce

From the repo root:

```sh
bash tests/c_standards/real_projects/run.sh
```

It re-probes clang and regenerates all 66 cells. Machine-readable results:
`tests/c_standards/real_projects/logs/results.tsv`. Per-cell evidence (one file
per project × std × matrix):
`tests/c_standards/real_projects/logs/cells/<A|B>__<project>__<std>.evidence.txt`
— each contains the exact command, source hash, compile exit, complete driver
stdout/stderr, per-case run comparisons, and final status. Raw run captures are
under `logs/runs/`. All of these are inside the gitignored `logs/` directory.
