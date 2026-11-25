# Table-driven libc contract matrix (issue #25)

Run:

```sh
CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/libc_contract_matrix/run_suite.py
```

All 29 entries in `compiler/Analysis/libc_contracts.json` are listed. The 15
compiler-enforced contracts have a positive case that must be accepted and a
negative case that must be rejected with an asserted diagnostic substring. The
14 `_s` capability-layer entries are reported as PARTIAL: their contract
metadata is asserted, but no compiler diagnostic is claimed because their exact
signatures belong to the Agent 2 capability layer.

Exact commands, exit codes, stdout and stderr are in
`results/summary.{txt,json}`. No skips, hard timeouts.
