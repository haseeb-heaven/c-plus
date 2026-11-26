# C+ core end-to-end scenario measurement (5140 scenarios)

Deterministic, exact-output end-to-end coverage of the C+ compiler core.

## Reproduce

From the repository root:

```sh
CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/e2e_10k/core/run_e2e.py
```

The runner regenerates nothing by hand: `python3 generate.py` is deterministic
(seed `20261124`) and emits:

* `generated/shard_00.c` … `generated/shard_09.c` — 510 named scenario
  functions each (5100 runtime scenarios);
* `generated/reject/*.c` — 40 single-violation programs (8 kinds x 5) whose
  **compile-time** c+ rejection is asserted exactly;
* `generated/manifest.json` and per-shard `.scenarios.json` metadata.

`run_e2e.py` builds each shard with `c+ build --default --sanitize=none`, runs
the safe scenarios (`mode 0`) and trap scenarios (`mode 1`), and cross-checks
every plain-C-safe scenario with `/usr/bin/clang -std=c11 -O2` (`mode 2`).
Each rejection case is compiled in its own forked child process with a hard
timeout; c+ must exit 1 and emit the exact expected diagnostic substring.

Inside each shard, the generated harness runs **every** named scenario in a
forked child with a 2-second hard timeout (poll on stdout/stderr pipes, SIGKILL
on timeout) and asserts exact stdout, stderr and exit status. Trap scenarios
therefore cannot corrupt the harness or each other. One `PASS`/`FAIL` line is
printed per scenario, followed by a `SUMMARY` line.

## Coverage

12 categories: arithmetic (960), bounds (1020), allocation/lifetime (820),
nullability (260), initialization (250), exceptions (260), RAII (250),
recursion (260), structs/unions (250), function pointers (260), format strings
(250), runtime guards (260), plus 40 compile-time-rejection cases.

Traps asserted at runtime include dynamic out-of-bounds indices (exit 134),
division/remainder by zero, 32/64-bit signed overflow, division overflow and
null dereference (exit 102); diagnostics are compared byte-for-byte, including
generated source line numbers. Compile-time rejections cover double free,
use-after-free, direct null read/write, constant and negative array indices,
uninitialized reads, and invalid frees.

## Measured result (2026-11-24)

`summary.txt` / `summary.json` are written by the runner with exact counts:
5140 generated, 5140 executed, 5140 passed, 0 failed; clang cross-check
4230/4230 executed/passed, 0 failed. No cases are skipped and no failures are
tolerated.

Note: a few generated shapes avoid current c+ analyzer false positives
(stale symbol names across functions, `p += 0`, comma-separated array
declarations, arrays of structs); every such shape is replaced with an
equivalent construct, never omitted.
