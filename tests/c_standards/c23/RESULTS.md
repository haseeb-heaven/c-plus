# C23 support test — C+ compiler/driver (subagent 5/10)

Date: 2026-11-23 (UTC). Branch: `agent/modern-stdlib`. All artifacts and
commands live under `tests/c_standards/c23/`. Reproduce with:

```sh
bash tests/c_standards/c23/run.sh
```

Raw capture files are in `tests/c_standards/c23/logs/`.

## Host / toolchain

- macOS 27.2.0, arm64 (`Darwin Haseebs-MacBook-Pro.local 27.2.0 ... RELEASE_ARM64_T8112`).
- `/usr/bin/clang` = **Apple clang version 16.0.0 (clang-1600.0.26.6)**.
- C+ driver: `build/c+` (invoked as `./build/c+ build <file> -std=<val> --sanitize=none -o <bin>`).
- C23 library archive: `build/libc/libcplus_hardened.a` (present).

## 1. Feature probe — what actually compiles with plain clang `-std=c2x`

Probed first, exactly as instructed. Every feature below compiled cleanly
(exit 0, empty stderr) and produced the shown stdout:

| Feature | Probe source | `-std=c2x` result |
|---|---|---|
| `nullptr` | `.probe: int *p = nullptr; ... p == nullptr` | exit 0, stdout `1` |
| `typeof` | `.probe: typeof(42) x = 7;` | exit 0, stdout `7` |
| `0b` binary literal | `.probe: printf("%d", 0b1010)` | exit 0, stdout `10` |
| `#elifdef` | `.probe: #if 0 / #elifdef WANT / ...` | exit 0, stdout `elifdef-taken` |
| `__STDC_VERSION__` | `.probe: printf(... __STDC_VERSION__)` | exit 0, stdout `__STDC_VERSION__=202000` |

Genuineness check (default mode = C17, no `-std`): `nullptr` **fails** —

```
.../nullptr.c:2:27: error: use of undeclared identifier 'nullptr'
```

and `#elifdef` warns `[-Wc2x-extensions]`. `__STDC_VERSION__` is `202000L`
only in c2x mode (`201710L` in C17). So the features are genuinely C23-gated.

`source.c` uses `__STDC_VERSION__ >= 202000L` (hard `#error` guard), `0b`
binary literals, `typeof`, and `#elifdef`. It deliberately avoids `nullptr`
because the C+ frontend rejects it (see §4). Expected/observed output:

```
STDC=202000 mask=10 branch=elifdef-taken
```

## 2. `-std` spelling acceptance

Plain clang 16 (probe file, and again with `source.c`) — identical results:

| spelling | plain clang exit | first diagnostic |
|---|---|---|
| `c23`   | 1 | `error: invalid value 'c23' in '-std=c23'` |
| `c2x`   | 0 | (empty) |
| `gnu23` | 1 | `error: invalid value 'gnu23' in '-std=gnu23'` |
| `gnu2x` | 0 | (empty) |

Apple clang 16 only knows `c2x` / `gnu2x`; its own diagnostic lists the
supported values and ends with:

```
note: use 'c2x' for 'Working Draft for ISO C2x' standard
note: use 'gnu2x' for 'Working Draft for ISO C2x with GNU extensions' standard
```

The C+ driver `build/c+ ... -std=<val> --sanitize=none` produced exactly the
same matrix for `source.c`:

| spelling | C+ compile exit | C+ run exit | stdout | first diagnostic |
|---|---|---|---|---|
| `c23`   | 1 | — | — | `error: invalid value 'c23' in '-std=c23'` |
| `c2x`   | 0 | 0 | `STDC=202000 mask=10 branch=elifdef-taken` | (empty) |
| `gnu23` | 1 | — | — | `error: invalid value 'gnu23' in '-std=gnu23'` |
| `gnu2x` | 0 | 0 | `STDC=202000 mask=10 branch=elifdef-taken` | (empty) |

`--verbose` confirms the C+ driver **forwards the spelling verbatim** to the
clang backend: the cc1 command line contains `-std=c2x`, and the driver prints
`backend: /usr/bin/clang ... -std=gnu17 -std=c2x`. Therefore `c23`/`gnu23`
fail because the host clang does not recognize those spellings, not because
the C+ frontend strips or rewrites them. (A newer clang accepting `c23` would
inherit support automatically; none is installed here.)

## 3. Real downloaded projects via the C+ driver

Sources are the unmodified originals already in the repo (game_a `snake.c`,
game_b `2048.c`, cli_app `xdump.c`; see their `ORIGINAL_EVIDENCE.md`).

### Default (safe) mode — no binary produced

All six builds (3 projects × {`c23`,`c2x`}) **exit 1**. The diagnostics are
*identical* for `c23` and `c2x` and are C+ safety-analysis findings that occur
before `-std` reaches clang, e.g.:

```
snake.c:44:39: error: [CPLUS-BOUNDS] [CPLUS-SAFE-BOUNDS] negative array index on 'sx'
2048.c:433:2: error: [CPLUS-FFI] 'printf' is not async-signal-safe
xdump.c:62:26: error: [CPLUS-OWNER] variable-length stack allocation has no bounds check; use cplus_alloc
```

These are mode/safety failures, **not** C23-standard failures.

### `--legacy-c` mode — meaningful `-std` comparison

With `--legacy-c` added (documented "legacy C compatibility mode"):

| project | `-std=c23` | `-std=c2x` |
|---|---|---|
| game_a snake  | exit 1 — `error: invalid value 'c23' in '-std=c23'` | exit 0, binary built |
| game_b 2048   | exit 1 — same clang diagnostic | exit 0, binary built |
| cli_app xdump | exit 1 — same clang diagnostic | exit 0, binary built |

So for real projects `c23` fails at the backend while `c2x` builds.

### Run under `timeout 30`, compare to recorded originals

All `c2x` binaries ran with exit 0 and no timeout. Byte-for-byte comparison
against the recorded original outputs (saved in each project's evidence dirs):

| Case | Command | Result vs recorded original |
|---|---|---|
| game_a | `printf 'q' \| timeout 30 <snake>` | **IDENTICAL** stdout (44 bytes, `Game over! Score: 0`) |
| game_b | `timeout 30 <2048> test` | **IDENTICAL** (`All 13 tests executed successfully`) |
| game_b | `timeout 30 <2048> --version` | **IDENTICAL** (`2048.c version 1.0.3`) |
| game_b | `timeout 30 <2048> --help` | **IDENTICAL** |
| cli_app | `-v`, `-h` | **IDENTICAL** stdout + stderr |
| cli_app | `file_default`, `file_width8`, `file_skip_n`, `stdin_default` | **IDENTICAL** stdout |
| cli_app | `missing_file` | **IDENTICAL** stderr |
| cli_app | `-Z` | exit 1; stderr IDENTICAL after normalizing `argv[0]` (only difference: `/tmp/.../xdump` vs `./build/xdump` prefix) |

No fabricated pass: every comparison above came from an executed build/run
with captured files under `tests/c_standards/c23/logs/`.

## 4. C++-style `nullptr` is rejected by the C+ frontend

`nullptr` compiles with plain clang `-std=c2x` (§1) but the C+ driver rejects it
at its own lowering stage for **every** spelling:

```
.../nullptr.c:2:27: error: [CPLUS-FFI] C23 nullptr is not supported; use NULL
  int main(void) { int *p = nullptr; printf("%d\n", p == nullptr); return 0; }
                            ^~~~~~~
help: use C+ managed allocations and cplus_* helpers
.../nullptr.c:2:56: error: [CPLUS-FFI] C23 nullptr is not supported; use NULL
...
```

The other probed C23 features (`typeof`, `0b`, `#elifdef`, `__STDC_VERSION__`)
pass the C+ frontend unchanged. ⇒ C23 *language-feature* support through the
driver is **partial** even under a supported spelling.

## 5. C23 library API inventory (do NOT claim full C23 library coverage)

`nm -g build/libc/libcplus_hardened.a | ... | grep '^_cplus_' | sort -u` →
805 unique `cplus_*` symbols overall. The C23-related families actually
present (31 unique symbols):

| Family | Symbols present |
|---|---|
| `nextup` | `_cplus_nextup`, `_cplus_nextupf`, `_cplus_nextupl` |
| `nextdown` | `_cplus_nextdown`, `_cplus_nextdownf`, `_cplus_nextdownl` |
| `fadd` | `_cplus_fadd`, `_cplus_faddf`, `_cplus_faddl` |
| `fsub` | `_cplus_fsub`, `_cplus_fsubf`, `_cplus_fsubl` |
| `fmul` | `_cplus_fmul`, `_cplus_fmulf`, `_cplus_fmull` |
| `fdiv` | `_cplus_fdiv`, `_cplus_fdivf`, `_cplus_fdivl` |
| `hypot3` | `_cplus_c23_hypot3`, `_cplus_hypot3` |
| `f16` math | `_cplus_exp16`, `_cplus_frexp16`, `_cplus_fma16`, `_cplus_logb16`, plus `_cplus_c23_exp16`, `_cplus_c23_frexp16`, `_cplus_c23_fma16`, `_cplus_c23_logb16` |
| `timespec_getres` | `_cplus_timespec_getres` |
| `mbrtoc8` | `_cplus_mbrtoc8` |
| `c8rtomb` | `_cplus_c8rtomb` |

All API groups named in the task brief are present. This is a **partial**
C23 library surface — e.g. the rest of the `<math.h>` `_Float16` family,
`<stdckdint.h>` checked-integer macros, and other C23 additions were not
verified here; no claim of full C23 library coverage is made.

## 6. Classification

| Spelling | Classification | Evidence |
|---|---|---|
| `c2x` | **SUPPORTED** | Accepted by clang 16 and C+ driver (exit 0); `source.c` compiles/runs with correct C23 output; all 3 real projects build under `--legacy-c` and outputs match originals byte-for-byte. |
| `gnu2x` | **SUPPORTED** | Same as `c2x` in every tested case. |
| `c23` | **UNSUPPORTED (host toolchain)** | Rejected by `/usr/bin/clang` 16: `error: invalid value 'c23' in '-std=c23'`. C+ driver forwards it verbatim and inherits the rejection, for `source.c` and all 3 projects. Not a C+ front-end rewrite. |
| `gnu23` | **UNSUPPORTED (host toolchain)** | Rejected by `/usr/bin/clang` 16: `error: invalid value 'gnu23' in '-std=gnu23'`. Same forwarding behavior as `c23`. |

Qualifiers:
- Language coverage under supported spellings is **PARTIAL**: `nullptr` is
  rejected by the C+ frontend (`[CPLUS-FFI]`), while `typeof`, `0b`,
  `#elifdef`, `__STDC_VERSION__` pass.
- Real-project builds through the C+ driver require `--legacy-c`; default
  safe mode rejects these legacy C sources with safety diagnostics
  independent of `-std`.
- Library coverage is **PARTIAL** (31 C23 symbols inventoried; no full-coverage
  claim).

## Files delivered

- `tests/c_standards/c23/source.c` — minimal genuine C23 program.
- `tests/c_standards/c23/run.sh` — reproduction script (self-contained).
- `tests/c_standards/c23/RESULTS.md` — this report.
- `tests/c_standards/c23/logs/` — generated evidence captures (compile/run
  stdout/stderr, normalized comparisons, symbol inventory).

No files outside `tests/c_standards/c23/` were created or modified; no source
in `external_projects/` or `libc/` was edited; no `git add`/commit was run.
