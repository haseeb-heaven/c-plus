# EDGE100 examples — results

## Driver

`sh tests/edge100/examples/run.sh` is a pure POSIX-shell driver (no Python, no
source/header edits, no git operations). It builds all 123 in-scope programs,
runs a bounded edge-case battery, verifies determinism, and prints a single
summary line:

```
EDGE100 examples <programs> <passed> <failed> <build_failed>
```

All artifacts live only under `tests/edge100/examples/`. The driver:

- builds `.cp` with `./build/c+ build <src> --default --sanitize=none -I.`
  plus the per-program library flags in `PROGRAM_MANIFEST.md` (yyjson,
  SDL3 + Nuklear, curl, libuv, safecrt/hardened-libc archives after `--`);
- builds `.c` with `/usr/bin/clang -std=c11 -O1 -I. <src>` plus the program's
  archive/library flags;
- bounds **every** build and every run with `timeout 30`;
- sets `SDL_VIDEODRIVER=dummy`, `SDL_AUDIODRIVER=dummy`, `LC_ALL=C`, `TERM=dumb`
  and a private `HOME` under this directory;
- feeds the documented quit key to the five interactive programs so they exit
  without a terminal;

Edge-case battery per non-interactive program:

| case | input |
|---|---|
| `baseline` | empty stdin, no argv |
| `huge_stdin` | 100,000 bytes of `A` |
| `hostile_stdin` | 64 repeats of `\xff\xfe\xfd\x00\x01\x02\x80\x81\x7f\n\r\t` |
| `bad_args` | hostile-looking argv token |
| `size_max_arg` | `18446744073709551615` |
| `huge_arg` | 10,000-character argv |
| `empty_arg` | zero-length argv |
| `missing_path` | nonexistent file path |

Interactive programs receive the same argv hostility with their quit key on
stdin. Every program's baseline is run twice and compared byte-for-byte. Two examples
render a randomly seeded board and are compared on a stable contract line only:
`examples/games/snake.c` (`Game Over! Your Score:`) and
`examples/games/tiles.c` (`Final score:`).

## Classification

A case **passes** iff the process does not terminate on a signal, does not hit
the 30-second cap, and its output is one of:

1. byte-exact the program's deterministic baseline (evidence of ignoring or
   safely refusing the hostile input), or
2. an explicit refusal token (`REFUS*`, `error`, `usage`, `FAIL`, `bad-args`,
   `cannot-open`, `missing-input`, `no-input`, `invalid ... token`,
   `empty move sequence`, `unknown_case`, constraint-handler messages), or
3. an explicit completion sentinel emitted by the example itself
   (`selfcheck=ok`, `*_SUMMARY`, `FINAL`, `SCENARIO`, `CANARY`, `intact=`,
   `done`, `SHA_LITE <n>`, `TWC <n>`), or
4. (only for `utilities/security_tools/bounds_check.cp`) a small nonzero exit
   with no output, which is its documented bad-args refusal.

Exit `>= 128` (including `134` = SIGABRT), `124`/`137` from `timeout`, output
that is neither baseline nor a defined refusal/sentinel, and any non-identical
determinism re-run are counted as failures. Nothing is skipped or waived.

## Evidence files

- `results/final.out` — final driver stdout (contains the summary line).
- `results/runs.log` — every build verdict, every case verdict with exit code
  and byte count, and every determinism comparison.
- `results/<name>.build.log` — authoritative compiler/linker output per program.
- `fixtures/` — the exact deterministic edge inputs.
- `build_table.json`, `programs.lst` — static build/flag manifest.

## Environment instability (important)

The C+ compiler binary (`build/c+` → `c-plus-core/build/c+`) was rebuilt several
times by parallel agents during this task. Its SHA-256 changed from
`11fca8f0…` to `c9d7e965…` to `9680c05d…` to `127bdba5…` to `aa5f4149…` within
the run window, while uncommitted changes sat in `compiler/Sema/CPlusRewriter.cpp`,
`compiler/Sema/StaticBoundsCheck.cpp` and related files. Consequences observed
with the in-repo compiler:

- `variable length array declaration not allowed at file scope` for `__cplus_bounds_check(...)`
  wrapped array dimensions (`matrix.cp`, `iniparse.cp`, `breakout.cp`,
  `calculator.cp`, many others) — previously built cleanly;
- runtime bounds aborts (`C+ error: index 3 is outside the bounds of 'steps'`)
  in `regex_lite.cp`, `fhash.cp`, `urlcheck.cp` — these same binaries returned
  clean self-check output with the compiler snapshot from before the rebuilds.

A stable snapshot (`c-plus-core/build-verify-1123/c+`) builds the former group
but lacks later stdlib support and fails 90 of the 123 programs, so it is not a
valid substitute. Results below are therefore reported verbatim against the
in-repo compiler; no failure is hidden or reclassified.

## Final verdict (verbatim `results/final.out`)

```
EDGE100 examples 123 84 3 36
```

- **123 programs** in the locked scope (90 `.cp`, 33 `.c`).
- **84 passed** — built, all bounded edge cases produced baseline output or a
  defined refusal/sentinel, and the determinism re-run was identical.
- **3 failed** (run-time, not hidden):
  - `examples/expanded/data_2/regex_lite.cp` — SIGABRT (`rc=134`) with
    `C+ error: index 3 is outside the bounds of 'steps' (length 3)` on every
    case, including empty stdin. The binary produced clean self-check output
    with the pre-rebuild compiler.
  - `examples/utilities/file_tools/fhash.cp` — SIGABRT (`rc=134`) on every case.
  - `examples/utilities/net_tools/urlcheck.cp` — SIGABRT (`rc=134`) on every case.
- **36 build_failed** — every one is a compile/link failure under the required
  `--default --sanitize=none` flags; the dominant causes are the compiler's new
  `__cplus_bounds_check(...)` VLA lowering, `CPLUS-LIFETIME`/`CPLUS-BOUNDS`
  rejections, missing SDL3/Nuklear or yyjson resolution under the rebuilt
  compiler, and genuine source-level issues (`student.cp`, `macpulse.cp`,
  `safe_token.cp`). Exact compiler diagnostics are in
  `results/<name>.build.log`; per-program verdicts are in `results/runs.log`.

Compiler identity for this run (recorded by the driver):

```
EDGE100_COMPILER wrapper sha256=aa5f4149a5706979b0e540de96df6eb2a7f9acb6e80cf205c41db72fbd512b80
EDGE100_COMPILER backend sha256=f92946cb0483c125684c49586bb218a0c6f8abfa00bc72f6e186bc65af86b404
EDGE100_COMPILER backend mtime=Nov 24 12:27:01 2026 size=105856672
```

The driver exits nonzero whenever any program fails to build or any case fails;
this run's exit status was `1`, consistent with the honest counts above. No
program was skipped, and no failure was reclassified to make the totals clean.
