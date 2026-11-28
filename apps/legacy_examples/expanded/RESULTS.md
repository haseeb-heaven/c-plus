# Expanded examples — aggregate results

Recorded from a **real run** of `sh apps/legacy_examples/expanded/run_all.sh` from the
repository root on the toolchain below. This is a snapshot, not a promise: a
group that had not landed at snapshot time is marked **PENDING** with its exact
reproduction command. Nothing in this file is fabricated or inferred from a
build alone.

## Environment / toolchain

| Item | Value |
| --- | --- |
| Snapshot time (UTC) | `2026-11-23T06:13:10Z` |
| Host | Darwin arm64, macOS 27.2.1 |
| Backend clang (pinned) | `Apple clang version 16.0.0 (clang-1600.0.26.6)`, `Target: arm64-apple-darwin27.2.0`, `/usr/bin/clang` |
| C+ driver | `C+ 0.1.0` / `Language: C+ (C with memory safety, deterministic RAII, lightweight classes)` |
| Rust (comparison mirrors only) | `rustc 1.100.0 (0ef1bc386 2026-11-09)` |
| Runner | `/usr/bin/timeout` with `-k 10 300` |
| Child PATH | `/usr/bin:/bin:/usr/sbin:/sbin` plus only the directories that actually provide `c+`, `rustc`, `perl`, `awk` |
| `CPLUS_CLANG` | `/usr/bin/clang` (Homebrew clang deliberately kept off the child PATH) |

Verification performed before the run: `sh -n apps/legacy_examples/expanded/run_all.sh`
→ `sh -n: OK`. The aggregate runner uses no Python and no success-override
(`|| true`) masking.

## Exact final table (verbatim stdout)

```
expanded-examples aggregate: timeout=300s (kill after 10s), PATH=/usr/bin:/bin:/usr/sbin:/sbin:/Users/haseeb-mir/.local/bin:/Users/haseeb-mir/.cargo/bin, CPLUS_CLANG=/usr/bin/clang

RUNNER                 TOKEN             EXIT  STATUS       FINAL LINE (verbatim)
------                 -----             ----  ------       ------------------
cli                    CLI_EXAMPLES      0     PASS         CLI_EXAMPLES 4 4 0
database               DB_EXAMPLES       0     PASS         DB_EXAMPLES 3 3 0
engine                 ENGINE_EXAMPLES   0     PASS         ENGINE_EXAMPLES 3 3 0
games/cores            GAME_CORES        0     PASS         GAME_CORES 3 3 0
games/snake            SNAKE_EXAMPLES    0     PASS         SNAKE_EXAMPLES 3 3 0
(missing:SAFETY_DEMOS) SAFETY_DEMOS      -     PENDING      PENDING - no runner reported SAFETY_DEMOS

expanded-agg: runners=5 reported=5 pass=5 blocked=0 fail=0 hard_fail=1
expanded-agg: expected tokens: CLI_EXAMPLES SNAKE_EXAMPLES GAME_CORES DB_EXAMPLES ENGINE_EXAMPLES SAFETY_DEMOS
expanded-agg: logs: /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/expanded_examples/<runner>.log ; final lines: /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/expanded_examples/final_lines.txt
```

Exact final report lines captured verbatim (also saved in
`build/expanded_examples/final_lines.txt`):

```
CLI_EXAMPLES 4 4 0
DB_EXAMPLES 3 3 0
ENGINE_EXAMPLES 3 3 0
GAME_CORES 3 3 0
SNAKE_EXAMPLES 3 3 0
```

Exact stderr from the same run:

```
expanded-agg: PENDING SAFETY_DEMOS - rerun after that group's run.sh lands
```

## Totals and interpretation

| Metric | Value |
| --- | --- |
| Runners discovered | 5 |
| Runners that emitted a final line | 5 |
| PASS | 5 |
| BLOCKED (reported, not a hard failure) | 0 |
| Real FAIL / FAIL_EXIT / NO_REPORT / BAD_TOKEN | 0 |
| Expected tokens | 6 |
| Tokens reported | 5 of 6 |
| PENDING tokens | 1 (`SAFETY_DEMOS`) |
| `hard_fail` | 1 (caused **only** by the missing safety group) |
| Aggregate process exit | **1** |

The aggregate exit of 1 is **not** a failing example: all five landed groups
passed. It reflects the runner's integrity rule that a missing expected group
is a real failure and must never be silently counted as a pass. Once the safety
`run.sh` lands, a clean run is expected to exit 0.

Per-runner stdout/stderr logs are at
`build/expanded_examples/<runner>.log` (`cli.log`, `database.log`, `engine.log`,
`games_cores.log`, `games_snake.log`).

## Reproduction commands

Whole campaign (discovers every `<group>/*/run.sh` and `<group>/run.sh`):

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
sh -n apps/legacy_examples/expanded/run_all.sh
sh apps/legacy_examples/expanded/run_all.sh
```

The runner applies this environment to every child automatically; to reproduce
a single group under the same environment:

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
PATH=/usr/bin:/bin:/usr/sbin:/sbin:"$(dirname "$(command -v c+)")" \
CPLUS_CLANG=/usr/bin/clang LC_ALL=C \
  timeout 300 sh apps/legacy_examples/expanded/cli/run.sh
```

Replace `cli` with `database`, `engine`, `games/cores` or `games/snake` for the
other landed groups. Each group owns its own build commands and per-case
evidence in its `run.sh` and `NOTES.md`; the aggregate does not rebuild
examples itself.

## PENDING group — exact command

The safety group had source files present (`bounds_demo.cp`,
`memcpy_s_demo.cp`) but no `run.sh` / final report at snapshot time. No result
is claimed for it. Re-run after it lands:

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib

find apps/legacy_examples/expanded/safety -type f | sort
sh apps/legacy_examples/expanded/safety/run.sh

# Refresh the whole table (all six tokens then present):
sh apps/legacy_examples/expanded/run_all.sh
```

## Rust comparison mirrors

`benchmarks/example_rust_compare/expanded_cli_games/compare.sh` and
`benchmarks/example_rust_compare/expanded_db_engine/compare.sh` are sibling-owned
comparison harnesses and are **not** run by `apps/legacy_examples/expanded/run_all.sh`.
Their own protocol is `EXAMPLE_COMPARE <name> MATCH|DIFFERENT|BLOCKED <reason>`
and their own evidence lives under `benchmarks/example_rust_compare/`:

```sh
sh benchmarks/example_rust_compare/run_all.sh
```

This document makes no claim about those harnesses' outcomes.

## Explicit non-claims

- **No Rust-equivalence claim.** Any `MATCH` / matching transcript produced by
  the comparison mirrors means observable behavior parity for the *tested
  inputs only*. It is not a claim that C+ and Rust are equivalent languages or
  that their safety mechanisms are the same.
- **No 100% safety claim.** C+ combines compiler analysis with runtime checks;
  Rust uses compile-time ownership/borrowing plus language-runtime checks. The
  safety demos are runtime *refusal evidence*, not proofs of memory safety.
- **Matching output ≠ equivalent guarantees.** Byte-identical stdout/stderr for
  a fixed seed and bounded input says nothing about untested inputs, longer
  executions, concurrent use, or the strength of the underlying checks.
- **Not production software.** Every example is a deterministic teaching
  model — no production app, game, database or engine is claimed here.
- **macOS arm64 only.** Evidence was produced on Darwin arm64 with Apple clang
  16.0.0. No other OS or architecture is claimed, and a build alone is never
  presented as live-game or cross-platform success.
- **PENDING is not a pass.** A group without a landed runner is recorded with
  its exact reproduction command, never with an assumed result.
