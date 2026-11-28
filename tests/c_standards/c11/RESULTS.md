# C11 support in the C+ compiler driver — subagent 3/10

All commands below were executed on this machine. Raw, machine-generated output is in
`artifacts/run-report.log`; per-build diagnostics and captured program outputs are in
`artifacts/`. Reproduce everything with:

```
bash tests/c_standards/c11/run.sh
```

No file outside `tests/c_standards/c11/` was created or modified. Nothing under
`external_projects/` was edited (the originals were only read). No git add/commit was done.

## Environment

- Date of run: `2026-11-23T06:55:36Z`
- backend: `Apple clang version 16.0.0 (clang-1600.0.26.6)`, macOS arm64
- driver: `./build/c+` (Mach-O arm64 executable)

---

## 1. The minimal genuine C11 program (`source.c`)

`source.c` uses three features that do not exist in C99/C89:

1. `<stdatomic.h>` — `atomic_int`, `atomic_init`, `atomic_fetch_add`, `atomic_load`
2. `_Static_assert` — C11 compile-time assertion keyword
3. `_Generic` — C11 compile-time type-selection keyword

Its single expected stdout line is `int counter=7`, which simultaneously proves that
`_Static_assert` passed, `atomic_fetch_add` worked, and `_Generic` selected the `int` arm.

### Reference build (plain `/usr/bin/clang -std=c11 -pedantic-errors`)

```
/usr/bin/clang -std=c11 -pedantic-errors tests/c_standards/c11/source.c -o artifacts/ref_c11
```
- exit: **0**, diagnostics: **empty**
- `artifacts/ref_c11` → exit **0**, stdout: `int counter=7`

The same reference compile was also run with `-std=c1x` and `-std=gnu11`: both exit **0**,
empty diagnostics, and print `int counter=7`.

### Negative control (proves the source is really C11-only)

```
/usr/bin/clang -std=c99 -pedantic-errors tests/c_standards/c11/source.c -o artifacts/ref_c99_fail
```
- exit: **1**, diagnostics (verbatim from `run-report.log`):

```
.../source.c:14:1: error: '_Static_assert' is a C11 extension [-Werror,-Wc11-extensions]
   14 | _Static_assert(sizeof(int) >= 4, "int must be at least 32 bits");
      | ^
.../source.c:18:12: error: '_Generic' is a C11 extension [-Werror,-Wc11-extensions]
   18 |     return _Generic(x,
      |            ^
2 errors generated.
```

---

## 2. C+ driver with each `-std` spelling (minimal C11 program)

For each spelling the exact command was:

```
./build/c+ build tests/c_standards/c11/source.c -std=<SPELLING> --sanitize=none -o artifacts/src_<SPELLING>
```

| spelling | build exit | build diagnostics | run exit | stdout |
|---|---|---|---|---|
| `c11`   | 0 | 0 bytes | 0 | `int counter=7` |
| `c1x`   | 0 | 0 bytes | 0 | `int counter=7` |
| `gnu11` | 0 | 0 bytes | 0 | `int counter=7` |

### Pass-through evidence (`--verbose` backend command line)

The C+ driver hard-codes a backend default (`-std=gnu17`), then appends the user's
`-std=` **last**, so the user's spelling is the one clang actually uses. Token counts
from the per-spelling `artifacts/src_<spelling>.verbose.log`:

| spelling | `-std` tokens seen in verbose log |
|---|---|
| `c11`   | `2 -std=c11`, `1 -std=gnu17` |
| `c1x`   | `2 -std=c1x`, `1 -std=gnu17` |
| `gnu11` | `2 -std=gnu11`, `1 -std=gnu17` |

The clang `-cc1` invocation itself (captured verbatim) carries `-x c ... -std=c11`
(or `-std=c1x` / `-std=gnu11`), confirming the value reaches the compiler and that
the lowered temporary file is compiled as C. No `-Wc11-extensions`, deprecation, or
invalid-standard warning was emitted for any spelling.

### Strictness caveat (recorded, not a C11 failure)

The driver does not add `-pedantic-errors`. Consequently this C11-only source also
builds under its `-std=c99` spelling:

```
./build/c+ build source.c -std=c99 --sanitize=none -o artifacts/src_c99
```
- exit **0**, diagnostics **0 bytes**

So the C+ driver passes `-std` through correctly and compiles C11, but it is **not a
strict conformance checker**: it will not reject post-C99 constructs when a pre-C11
`-std` is requested. This does not affect the C11 spellings under test.

---

## 3. Real downloaded projects via the C+ driver with `-std=c11`

Projects (original, byte-for-byte preserved sources, read only):

- `game_a`: `external_projects/game_a/original/snake.c` (Corg-Labs/snake)
- `game_b`: `external_projects/game_b/original/2048.c` (mevdschee/2048.c)
- `cli_app`: `external_projects/cli_app/original/xdump.c` (xfgusta/xdump)

### SAFE-mode note

With default SAFE mode the driver rejects all three sources — but for C+ *safety*
diagnostics (`CPLUS-BOUNDS`, `CPLUS-LIFETIME`, `CPLUS-OWNER`, `CPLUS-FFI`), e.g.:

```
external_projects/game_a/original/snake.c:44:39: error: [CPLUS-BOUNDS] [CPLUS-SAFE-BOUNDS] negative array index on 'sx'
external_projects/game_b/original/2048.c:433:2: error: [CPLUS-FFI] 'printf' is not async-signal-safe
external_projects/cli_app/original/xdump.c:62:26: error: [CPLUS-OWNER] variable-length stack allocation has no bounds check
```

These are unrelated to the C language standard. `--legacy-c` is the driver's
documented mode for compiling legacy C, so it is used below; the `-std=c11` spelling
is still passed verbatim. Verbose check under `--legacy-c` shows the tokens
`2 -std=c11`, `1 -std=gnu89` — the user's `-std=c11` is again last and wins.

### Builds

```
./build/c+ build external_projects/game_a/original/snake.c -std=c11 --sanitize=none --legacy-c -o artifacts/game_a_c11
./build/c+ build external_projects/game_b/original/2048.c  -std=c11 --sanitize=none --legacy-c -o artifacts/game_b_c11
./build/c+ build external_projects/cli_app/original/xdump.c -std=c11 --sanitize=none --legacy-c -o artifacts/cli_app_c11
```

| project | exit | diagnostics |
|---|---|---|
| game_a | 0 | 0 bytes |
| game_b | 0 | 0 bytes |
| cli_app | 0 | 0 bytes |

### Runs under `timeout 30` with fixed inputs, vs recorded originals

`cmp_exit=0` means byte-for-byte identical stdout to the recorded upstream original.

| project | exact run command | exit | stdout bytes | cmp vs recorded |
|---|---|---|---|---|
| game_a | `printf 'q' \| timeout 30 artifacts/game_a_c11` | 0 | 44 | **0** (identical) |
| game_b | `timeout 30 artifacts/game_b_c11 test` | 0 | 35 | **0** |
| game_b | `timeout 30 artifacts/game_b_c11 --version` | 0 | 21 | **0** |
| game_b | `timeout 30 artifacts/game_b_c11 --help` | 0 | 338 | **0** |
| game_b | `timeout 30 artifacts/game_b_c11 < quit_input.txt` (`qy`) | 0 | 1492 | 1 — see note |
| cli_app | `timeout 30 artifacts/cli_app_c11 -v` | 0 | 20 | **0** |
| cli_app | `timeout 30 artifacts/cli_app_c11 -h` | 0 | 433 | **0** |
| cli_app | `timeout 30 artifacts/cli_app_c11 <fixture>` | 0 | 313 | **0** |
| cli_app | `timeout 30 artifacts/cli_app_c11 -w 8 -g 4 <fixture>` | 0 | 334 | **0** |
| cli_app | `timeout 30 artifacts/cli_app_c11 -s 32 -n 16 <fixture>` | 0 | 88 | **0** |
| cli_app | `timeout 30 artifacts/cli_app_c11 < fixture` (stdin) | 0 | 313 | **0** |
| cli_app | `timeout 30 artifacts/cli_app_c11 -Z` | 1 | 0 | **0** |
| cli_app | `timeout 30 artifacts/cli_app_c11 /definitely/not/a/file` | 0 | 0 | **0** |

Notes on the two non-zero/differing observations:

- **game_b stdin quit**: both outputs are exactly 1492 bytes; byte-level `cmp` is 1
  because the board is randomly seeded from `time(NULL)` (the recorded upstream
  evidence states the tile positions vary per run). The control flow is identical:
  `QUIT? (y/n)` prompt count = 1, `Cannot read keyboard` count = 0, same legend and
  terminal-restore tail after stripping ANSI escapes. No timeout or crash (exit 0).
- **cli_app `-Z`**: stdout is empty in both (cmp 0, exit 1 in both). Its stderr differs
  only in `argv[0]`: ours is `.../cli_app_c11: illegal option -- Z`, recorded is
  `./build/xdump: illegal option -- Z` — an artifact of the different binary path,
  not a behavioral difference.

No run hit the 30-second limit. The cli_app fixture was copied (read-only) from
`external_projects/cli_app/build/fixtures/sample.bin`; SHA-256
`13d85a5e73340137a5ae38d4b8d1bbe34ab2539ed2974fc5a457d44edc10972c` matches the
recorded fixture hash.

---

## 4. Classification per spelling

| `-std` spelling | Classification | Evidence |
|---|---|---|
| `c11`   | **SUPPORTED** | Passed verbatim to clang cc1 (`-std=c11` last, overriding default `-std=gnu17`); genuine C11 program builds (exit 0, 0 diagnostics) and runs → `int counter=7`; all three real projects build under `--legacy-c` and reproduce recorded originals byte-for-byte in every deterministic case. |
| `c1x`   | **SUPPORTED** | Accepted as a clang alias for C11; passed verbatim (`-std=c1x` last); builds the C11 program (exit 0, 0 diagnostics) and runs → `int counter=7`; plain-clang `-std=c1x -pedantic-errors` reference also exit 0. Note: legacy/deprecated alias spelling, but fully functional with the shipped clang 16. |
| `gnu11` | **SUPPORTED** | Passed verbatim (`-std=gnu11` last); builds the C11 program (exit 0, 0 diagnostics) and runs → `int counter=7`; plain-clang `-std=gnu11 -pedantic-errors` reference also exit 0. |

**Overall:** C11 support in the C+ driver is `SUPPORTED` for all three spellings.
The spelling is never normalized or dropped — it is appended last to the backend
command line and honored by clang. Caveats (strictness, SAFE-mode rejection of legacy
sources, time-seeded game output) are documented above and are not C11-standard
defects.
