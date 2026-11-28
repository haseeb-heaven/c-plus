# RAII cleanup / control-flow regression suite

Run:

```sh
CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/raii_cleanup/run_suite.py
```

Five negative cases must be rejected by STRICT (multi-declarator owners, goto
into/out of an owning scope, setjmp assignment form, longjmp from a callback).
Sixteen positive cases must build and run with exact destructor-counter
stdout, including nested shadowing order, loops, switch/break, return, move,
continue, an explicit helper drop and eight generated control-flow fuzz
variants.

This suite complements — and does not duplicate — `tests/raii_shadowing/`
(identity-based shadow fixtures), `tests/core/` (basic RAII and move cases),
`tests/compiler/red_blue_round/` (throw/try cases) and
`tests/compiler/exceptions/` (exception fuzz).

Evidence: `results/summary.txt` and `results/summary.json` contain exact
commands, exit codes, stdout and stderr. Hard timeouts, no skipped cases.
