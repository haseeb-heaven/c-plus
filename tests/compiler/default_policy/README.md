# Default strict/warnings-as-errors policy (issue #34)

Run:

```sh
CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/default_policy/run_suite.py
```

The suite proves that clean C and C+ programs build without flags, that both C+
warnings and enabled host-Clang warnings fail `check`/`build` by default, that
`-Werror` is redundant, that `-w` cannot bypass C+ warnings or safety gates in
Safe mode, that `--release` does not weaken the policy, that `--legacy-c` is an
explicit compatibility opt-out, that a warning failure leaves no executable,
and that `--no-werror` is a diagnostic-only opt-out.

Evidence: `results/summary.txt` and `results/summary.json` contain exact commands,
exit codes, stdout and stderr for every case. Hard timeouts; no `|| true`, skips
or suppressed failures.
