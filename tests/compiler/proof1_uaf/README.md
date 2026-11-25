# Pointer-to-pointer release-effect suite

Run:

```sh
CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/proof1_uaf/run_suite.py
```

`AstSafetyCheck` now records `free(*pp)` / `free(pp[0])` as a pointee release
effect on the callee's parameter slot and transfers that effect to a caller
passing `&p`. This suite rejects the Proof 1 double-pointer UAF and the
array-index spelling, and proves that benign read/write indirection still
compiles and runs. Exact commands, exit codes, stdout and stderr are in
`results/summary.{txt,json}`. Hard timeouts, no skips.
