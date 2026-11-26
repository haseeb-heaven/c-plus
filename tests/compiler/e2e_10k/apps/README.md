# e2e_10k/apps — end-to-end scenario suite

Measured suite: **6,828 scenarios** in **12 C files**, covering all **209**
exported safe-C APIs plus app/game patterns (tic-tac-toe, 2048, snake,
calculator, line editor, hash, pthread, file I/O, JSON).

Every scenario runs in a forked child. The parent asserts exact stdout,
exact stderr (diagnostics) and exact exit status; trap scenarios (exit 70)
are asserted the same way. Expectations are frozen in `exp.json` from two
identical `/usr/bin/clang -std=c11 -O2` reference runs (`--bake`). The normal
run rebuilds with those expectations embedded, runs clang and c+ separately,
and compares each scenario byte-for-byte as a differential check.

## Usage

```sh
CPLUS_BIN=/path/to/c+ STDLIB_ROOT=/path/to/c-plus-stdlib \
  python3 run_suite.py --bake   # only when changing scenarios
CPLUS_BIN=/path/to/c+ STDLIB_ROOT=/path/to/c-plus-stdlib \
  python3 run_suite.py          # writes summary.txt / summary.json
```

`STDLIB_ROOT` is required for a trustworthy run; the sibling checkout is used
only as a local default. Generated C, binaries, scratch files and caches are
ignored by `.gitignore`. Hard timeouts: 300 s per compile and per binary run,
10 s SIGALRM watchdog per scenario child.

All scenarios are built and run with `c+ build --legacy-c --sanitize=none`.
The representative app file `app_board.c` is additionally built and run with
`c+ build --default --sanitize=none`. `--strict` was attempted and is **not
applicable** to this fork/capture harness: this c+ version reports
`[CPLUS-ANALYSIS] cannot build the C+ safety AST for this translation unit`
for the runner. No scenarios are skipped: a failed build, signal exit, summary
mismatch, or expectation/differential mismatch makes the runner exit non-zero
and records the failure path in the summary.
