# C17 / C18 standard-support test results (subagent 4/10)

Status: **all five spellings SUPPORTED** by the C+ compiler driver, with real-project
execution evidence. Every command below was executed on this machine; raw logs are
under `tests/c_standards/c17/artifacts/`. Nothing is fabricated.

## 1. Scope and file ownership

Files created (all inside `tests/c_standards/c17/` only):

```
tests/c_standards/c17/source.c                 # genuine C17 probe program
tests/c_standards/c17/run.sh                   # regenerates all raw evidence
tests/c_standards/c17/RESULTS.md               # this document
tests/c_standards/c17/artifacts/               # raw logs, copies, binaries
```

No source edits, no `git add`/commit, and no writes to `external_projects/` were
performed. The C+ driver writes lowered-C temp files beside its input source, so
the three real-project sources were copied **byte-for-byte** into `artifacts/`
and the copies verified with `cmp` before the driver touched them
(`copy_game_a.exit`, `copy_game_b.exit`, `copy_cli_app.exit`, `copy_fixture.exit`
are all `0`). The originals were only ever read.

## 2. Environment

```
$ /usr/bin/clang --version
Apple clang version 16.0.0 (clang-1600.0.26.6)
Target: arm64-apple-darwin27.2.0

$ ./build/c+ --version
C+ 0.1.0
Backend: host Clang/LLVM

platform: macOS (darwin), arm64
branch:   agent/modern-stdlib
```

## 3. The probe program (`source.c`)

A genuine, runnable C17 (ISO/IEC 9899:2018) program. C17 is the bug-fix edition
of C11 and adds no new language features, so the probe deliberately uses C11
constructs carried unchanged into C17 in areas touched by defect reports:

- `<stdalign.h>` `alignas` / `alignof` plus `_Alignof` (DR 460 alignment)
- `_Noreturn` specifier plus `<stdnoreturn.h>`
- `_Static_assert`, `_Generic`, `<stdatomic.h>` atomics

It must compile under `-std=c17 -pedantic-errors` with **zero diagnostics** and
print exactly:

```
c17: alignof(int)=4 type=int counter=7
```

A negative control (`-std=c89`, §6) proves these constructs genuinely require
C11 or later, so a passing result cannot come from the program being accepted
as C89.

## 4. Per-spelling results

For each spelling `s` the exact commands were:

```
/usr/bin/clang -std=<s> -pedantic-errors tests/c_standards/c17/source.c -o tests/c_standards/c17/artifacts/clang_<s>
timeout 30 tests/c_standards/c17/artifacts/clang_<s>

./build/c+ build tests/c_standards/c17/source.c -std=<s> --sanitize=none -o tests/c_standards/c17/artifacts/cplus_<s>
timeout 30 tests/c_standards/c17/artifacts/cplus_<s>
```

(`:` in `iso9899:*` spelling is replaced by `_` in artifact filenames.)

| Spelling | plain clang `-std=<s> -pedantic-errors` | diagnostics | C+ driver build | diagnostics | program exit | stdout | Class |
|---|---|---|---|---|---|---|---|
| `c17` | exit 0 | empty (0 bytes) | exit 0 | empty (0 bytes) | 0 | expected line | **SUPPORTED** |
| `c18` | exit 0 | empty (0 bytes) | exit 0 | empty (0 bytes) | 0 | expected line | **SUPPORTED** |
| `iso9899:2017` | exit 0 | empty (0 bytes) | exit 0 | empty (0 bytes) | 0 | expected line | **SUPPORTED** |
| `iso9899:2018` | exit 0 | empty (0 bytes) | exit 0 | empty (0 bytes) | 0 | expected line | **SUPPORTED** |
| `gnu17` | exit 0 | empty (0 bytes) | exit 0 | empty (0 bytes) | 0 | expected line | **SUPPORTED** |

Exact per-spelling evidence files: `artifacts/clang_<s>.{exit,stdout,stderr,run.exit,run.stdout,run.stderr}`
and `artifacts/cplus_<s>.{exit,stdout,stderr,run.exit,run.stdout,run.stderr}`.
The machine-readable form is `artifacts/results.tsv`:

```
spelling         real_std_flags                  clang_exit clang_stderr_bytes cplus_exit cplus_stderr_bytes cplus_run_exit cplus_run_matches_expected classification
c17              -std=gnu17 -std=c17             0          0                  0          0                  0              YES                     SUPPORTED
c18              -std=gnu17 -std=c18             0          0                  0          0                  0              YES                     SUPPORTED
iso9899:2017     -std=gnu17 -std=iso9899:2017    0          0                  0          0                  0              YES                     SUPPORTED
iso9899:2018     -std=gnu17 -std=iso9899:2018    0          0                  0          0                  0              YES                     SUPPORTED
gnu17            -std=gnu17 -std=gnu17           0          0                  0          0                  0              YES                     SUPPORTED
```

### 4.1 Evidence that `-std=` is genuinely forwarded to clang

`./build/c+ build ... --verbose` shows the backend clang argument list contains
both the driver's own default and the user's spelling, with the user's spelling
last (clang uses last-wins for `-std`):

```
backend: /usr/bin/clang
  -std=gnu17
  -std=c17        # (or c18 / iso9899:2017 / iso9899:2018 / gnu17)
```

Captured in `artifacts/cplus_<s>.effective_std`.

An invalid spelling is not silently ignored — it is rejected by clang:

```
$ ./build/c+ build tests/c_standards/c17/source.c -std=bogus --sanitize=none -o ...
error: invalid value 'bogus' in '-std=bogus'
note: use 'c17', 'iso9899:2017', 'c18', or 'iso9899:2018' for 'ISO C 2017' standard
note: use 'gnu17' or 'gnu18' for 'ISO C 2017 with GNU extensions' standard
exit: 1
```

Full text: `artifacts/neg_bogus_cplus.stderr` (801 bytes, exit 1).

Note on `gnu17`: it selects C17 **with GNU extensions** rather than strict ISO
C17. It is accepted and produces the same program output, but it is not a
strict-conformance spelling; the strict spellings are `c17`/`c18` and
`iso9899:2017`/`iso9899:2018`.

Note on `--sanitize=none`: the driver accepts it (exit 0), but it does not appear
in the verbose compile or link argument lists (0 `sanitize` matches in
`cplus_c17.verbose.stderr`). It is effectively a no-op for this driver build;
it does not affect the `-std` result.

## 5. Real downloaded projects via the C+ driver with `-std=c17`

Sources: `game_a` = snake (Corg-Labs/snake), `game_b` = 2048
(mevdschee/2048.c), `cli_app` = xdump (xfgusta/xdump). Their preserved
`ORIGINAL_EVIDENCE.md` files document provenance, hashes and recorded original
outputs; those recorded outputs are the comparison baseline.

All builds used:

```
./build/c+ build --legacy-c tests/c_standards/c17/artifacts/<copy>.c -std=c17 --sanitize=none -o <binary>
```

`--legacy-c` is the driver's plain-C compatibility mode. It is needed here only
because the default **Safe** mode applies C+-specific static checks unrelated to
the C standard version; see §7. The verbose backend line confirms `-std=c17`
still wins over the legacy default:

```
  -std=gnu89
  -std=c17
```

(`artifacts/real_legacy.effective_std`)

### 5.1 Build results

| Project | Copy vs original (`cmp`) | C+ build exit | build stderr |
|---|---|---|---|
| game_a / snake | 0 | 0 | empty |
| game_b / 2048 | 0 | 0 | empty |
| cli_app / xdump | 0 | 0 | empty |

### 5.2 Run results under `timeout 30`, fixed inputs

| Case | Command | Exit | Compared stream | vs recorded original |
|---|---|---|---|---|
| game_a quit | `printf 'q' \| timeout 30 <snake>` | 0 | stdout (44 bytes) | **MATCH** |
| game_b self-test | `timeout 30 <2048> test` | 0 | stdout | **MATCH** |
| game_b version | `timeout 30 <2048> --version` | 0 | stdout | **MATCH** |
| game_b help | `timeout 30 <2048> --help` | 0 | stdout | **MATCH** |
| xdump version | `timeout 30 <xdump> -v` | 0 | stdout | **MATCH** |
| xdump help | `timeout 30 <xdump> -h` | 0 | stdout | **MATCH** |
| xdump file default | `timeout 30 <xdump> sample.bin` | 0 | stdout | **MATCH** |
| xdump width/group | `timeout 30 <xdump> -w 8 -g 4 sample.bin` | 0 | stdout | **MATCH** |
| xdump skip/length | `timeout 30 <xdump> -s 32 -n 16 sample.bin` | 0 | stdout | **MATCH** |
| xdump missing file | `timeout 30 <xdump> /definitely/not/a/file` | 0 | stderr | **MATCH** |
| xdump bad flag | `timeout 30 ./build/xdump -Z` (run from `artifacts/`) | 1 | stderr | **MATCH** |

All 11 comparisons are `MATCH` in `artifacts/comparisons.txt`. No case hit the
30-second timeout. The `sample.bin` fixture is byte-identical to the recorded
one (SHA-256 `13d85a5e73340137a5ae38d4b8d1bbe34ab2539ed2974fc5a457d44edc10972c`).

For the `bad_flag` case the binary is invoked as `./build/xdump` from inside
`artifacts/` (a copy is placed at `artifacts/build/xdump`) so `argv[0]` in
getopt's error message matches the recorded original exactly. Without that,
the only difference is the path prefix in the message — not a C17 behavior
difference.

The game_b stdin-driven interactive session was intentionally excluded from
byte comparison: its board is seeded from `time(NULL)`, so its rendered stdout
is nondeterministic even in the recorded original. Deterministic modes
(`test`, `--version`, `--help`) were used instead.

## 6. Negative controls (prove `-std` is honored)

| Control | Expected | Actual exit | First diagnostic |
|---|---|---|---|
| `/usr/bin/clang -std=c89 -pedantic-errors source.c` | fail | 1 | `error: '_Static_assert' is a C11 extension [-Werror,-Wc11-extensions]` |
| `./build/c+ build source.c -std=c89 --sanitize=none` | fail | 1 | `error: implicit declaration of function 'alignof' [-Werror,-Wimplicit-function-declaration]` |
| `./build/c+ build source.c -std=bogus --sanitize=none` | fail | 1 | `error: invalid value 'bogus' in '-std=bogus'` |

Full logs: `artifacts/neg_c89_plain.stderr` (3415 bytes),
`artifacts/neg_c89_cplus.stderr` (1614 bytes),
`artifacts/neg_bogus_cplus.stderr` (801 bytes).

## 7. Additional observations (not C17 defects)

- **Default Safe mode rejects the three real projects**, with C+ static-check
  diagnostics such as `[CPLUS-BOUNDS] negative array index`, `[CPLUS-LIFETIME]
  variable ... is uninitialized`, `[CPLUS-FFI] 'printf' is not
  async-signal-safe`, and `[CPLUS-OWNER] variable-length stack allocation has
  no bounds check`. Counts: game_a 16 errors, game_b 8 errors / 13 warnings,
  cli_app 3 errors / 4 warnings. These are C+ memory-safety policy diagnostics
  on legacy C idioms, not failures of `-std=c17`; the same sources build and run
  correctly in `--legacy-c` mode with the same `-std=c17` (§5). Raw logs:
  `artifacts/safe_<project>.build.stderr`.
- A C+ frontend false positive appeared during probe development: a helper
  function named `unreachable` was diagnosed as `[CPLUS-FFI] C23 unreachable()
  is not supported in safe C+`. The helper was renamed to `fatal_unexpected`;
  this is an identifier collision, unrelated to C17 support.
- The C+ backend hardcodes `-std=gnu89` (legacy mode) or `-std=gnu17` (safe
  mode), but the user's `-std=` value is appended afterwards and wins. This is
  why all five spellings take effect.

## 8. Classification summary

| Spelling | Classification | Basis |
|---|---|---|
| `c17` | SUPPORTED | clang 0/0 diagnostics; C+ 0/0 diagnostics; correct run; negative controls pass |
| `c18` | SUPPORTED | same |
| `iso9899:2017` | SUPPORTED | same |
| `iso9899:2018` | SUPPORTED | same |
| `gnu17` | SUPPORTED (with GNU extensions) | same, but not strict ISO mode |

Overall: **C17/C18 support in the C+ compiler is SUPPORTED for all tested
spellings**, with 11/11 real-project output comparisons matching the recorded
originals byte-for-byte.

## 9. Reproduction

```
cd <repo-root>
sh tests/c_standards/c17/run.sh
```

The script recompiles both toolchains for every spelling, re-runs all real
projects under `timeout 30`, refreshes `artifacts/results.tsv` and
`artifacts/comparisons.txt`, and prints both tables. It writes only inside
`tests/c_standards/c17/`.
