# Data-race regression suite

Run:

```sh
CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/data_race/run_suite.py
```

Four unsafe cases must be rejected by STRICT with the asserted diagnostic;
five paired safe cases must be accepted. The suite covers transitive global
writes through helper functions, missing unlocks, early returns while a mutex
is held, lock-order inversion, and the corresponding correctly synchronised,
atomic and consistently nested controls. Direct global races, stack borrows,
shared heap arguments and fork-in-threads are already covered elsewhere and
are deliberately not duplicated.

TSan is not used by this suite: it is unavailable with the default host clang
on this machine, and the driver's TSan probe reports that explicitly. The
checks here are compile-time AST analysis, not a runtime concurrency service.

Evidence: `results/summary.txt` / `results/summary.json` (exact commands, exit
codes, stdout, stderr; hard timeouts, no skips).
