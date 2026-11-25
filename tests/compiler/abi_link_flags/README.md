# Cross-TU / ABI link regression suite

Run:

```sh
CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/abi_link_flags/run_suite.py
```

The suite compiles a C helper with the host Clang into a static archive and a
shared library, links both through `c+ build` in DEFAULT, STRICT and LEGACY
modes, and asserts the exact program stdout. It also verifies that `check`
accepts linker-only flags and that an uncontracted allocation in one TU
released in another is rejected in STRICT but accepted in LEGACY.

This pins the fix where linker-only `-L`/`-l` inputs were wrongly passed to
the per-file `-c` step and promoted to hard errors by the default
warnings-as-errors policy. Broad layout/symbol/callback/variadic coverage is
already in `tests/compiler/abi_gate/` and is deliberately not duplicated.

Evidence: `results/summary.txt` and `results/summary.json` contain exact
commands, exit codes, stdout and stderr. Hard timeouts; no skipped cases.
