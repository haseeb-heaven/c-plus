# Pointer-provenance adversarial suite

Run:

```sh
CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/pointer_provenance/run_suite.py
```

Negative cases must be rejected by `c+ check --strict` and `c+ build --strict`
with the asserted diagnostic; positive cases must build and run with exact
stdout. The suite covers struct and nested-struct pointer fields, field plus
arithmetic, union integer-to-pointer punning, conditional aliases, integer
round-trips and identity-returning helpers. It does **not** duplicate the
already-proven direct cast, one-past-end, interior-free and cross-function
free paths (`tests/safety/`, `tests/compiler/semantic_adversarial/`,
`tests/core/`).

Evidence: `results/summary.txt` and `results/summary.json` contain exact
commands, exit codes, stdout and stderr. Hard timeouts; no skipped cases.
