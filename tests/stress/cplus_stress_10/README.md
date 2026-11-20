# C+ adversarial pack: 10 independent source files

Run from the repository root:

```sh
bash tests/stress/cplus_stress_10/run_10.sh
```

- Cases 01–07 must compile and produce exact expected output.
- Cases 08–09 must be rejected by `c+ check`.
- Case 10 must compile, then terminate with exit code 104.

The runner writes logs under `tests/out/stress/cplus_stress_10/` and returns
nonzero if any case fails. It is also wired into `tools/verify.sh`.

These expectations follow documented C+ behavior, but have NOT been executed against the user's local compiler. A failed case may reflect a compiler bug, a documentation/implementation gap, or an incorrect test expectation; inspect the logs before interpreting the result.
