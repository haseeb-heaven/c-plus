# Adversarial RAII cleanup-order suite

Fourteen deterministic cases exercise reverse-order destructor cleanup on
every non-exceptional exit path:

- nested `if`/`else` with early `return` (taken and untaken branches),
- `for` and `while` loops with `break` and `continue`,
- `return` from inside a loop,
- `switch` with `break` (including a `switch` nested in a loop),
- `goto` out of nested cleanup blocks, a same-block `goto`, a `goto` from a
  loop, and a chain of two `goto` cleanup blocks.

Fixtures use a non-idempotent resource destructor that prints `D<id>` on every
call, so a duplicate, missing or misordered drop is observable. Each case also
records a `cplus_alloc` pointer per resource in a fixed-size ledger; after all
scopes exit, `main` prints `live=<n>` where `<n>` counts ledger entries whose
`cplus_allocation_generation()` is non-zero. The public query is declared in
`runtime/include/cplus/cplus.h`; the global `cplus_alloc_live_count()` lives in
the hardened libc and is not linked into a default `c+ build` executable, so
this per-pointer query is used instead. All cases assert `live=0` and rc == 0.

Every case builds with `c+ build --default --sanitize=none` and runs under a
60 s build / 10 s run timeout. Build and run commands, exit codes, stdout and
stderr are recorded verbatim in `results/summary.json` and
`results/summary.txt`. No case is skipped and no failure is suppressed. The
four `goto` cases pass `--no-werror`: the frontend still emits the
"goto bypasses C+ cleanup" diagnostic, which remains in the captured stderr,
but default mode otherwise treats that warning as an error. No warning is
disabled in source.

Sanitizer variants are opt-in by evidence: a compile-and-run probe must prove
that the host backend supports `--sanitize=address` or `--sanitize=leak` before
any case is run with it. When the probe fails, the host is reported as
`UNTESTED_ON_HOST` together with the probe command and diagnostic; no pass is
claimed for that sanitizer.

## Run

```sh
CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/raii_adversarial/run_suite.py
```

Generated sources live in `cases/`; transient binaries and probe sources live
in the gitignored `results/bin/`.
