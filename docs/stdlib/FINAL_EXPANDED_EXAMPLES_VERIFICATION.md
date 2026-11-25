# Final verification — expanded examples suite

**Branch:** `agent/modern-stdlib`  **Date:** 2026-11-23  **Role:** final verification (agent 9 of 10)

This document records a clean, reproducible re-run of the four expanded-example
verification entry points. It is an observation log, not a new capability claim.
Only this document was authored by this agent; no source, script, or results file
was edited, and nothing was staged or committed. Generated artifacts under
`build/`, `tests/.../logs/`, and the two `compare.sh` output directories were
written by the suites themselves.

## Method / environment

Each suite was invoked exactly as specified, with the environment pinned and a
hard per-suite cap of 300 seconds:

```
env -u SDKROOT \
    CPLUS_CLANG=/usr/bin/clang \
    LC_ALL=C \
    PATH=/usr/bin:/bin:/usr/sbin:/sbin:/Users/haseeb-mir/.local/bin:/Users/haseeb-mir/.cargo/bin \
    timeout -k 10 300 sh <suite-script>
```

- `CPLUS_CLANG=/usr/bin/clang` (Apple clang); `SDKROOT` explicitly unset.
- Restricted `PATH`: base system directories plus only the tool directories
  required by the suites (`~/.local/bin` for `c+`, `~/.cargo/bin` for `rustc`).
  IDE/toolchain directories inherited from the interactive shell were removed.
- Host: macOS arm64. `rustc 1.100.0 (0ef1bc386 2026-11-09)`.
  `/bin/sh` is GNU bash 3.2.57; an additional diagnostic used
  `/opt/homebrew/bin/bash` 5.3.15.
- The aggregate and adversarial runners additionally impose their own inner
  timeouts (300 s and 120 s respectively); the two comparison suites impose a
  30 s build/run limit per case. No inner limit was reached in this run.

## Results

| Suite (prescribed command) | Final line (verbatim) | Exit | Interpretation |
| --- | --- | --- | --- |
| `sh examples/expanded/run_all.sh` | `expanded-agg: runners=6 reported=6 pass=6 blocked=0 fail=0 hard_fail=0` (last group line: `SAFETY_DEMOS 4 4 0`) | **0** | All six group runners reported PASS with zero failed cases; no `BLOCKED`, no `PENDING`, no timeout. |
| `sh tests/expanded_examples_adversarial/run_all.sh` | `ADV_EXPANDED_TOTAL 110 110 0 0` | **0** | 110 hostile-input cases executed; all 110 exited with a refusal/REFUSE sentinel; 0 crashed, 0 unexpected acceptance, 0 build failures. |
| `sh benchmarks/example_rust_compare/expanded_cli_games/compare.sh` | `EXPANDED_COMPARE snake_cli MATCH stdout+exit identical for 3 fixed case(s)` (stderr summary: `SUMMARY different=0 inconclusive(BLOCKED+PENDING)=0`) | **0** | 9/9 CLI-and-game examples matched their safe-Rust models byte-for-byte on stdout and identically on exit status; no `DIFFERENT`, no `BLOCKED`/`PENDING`. |
| `sh benchmarks/example_rust_compare/expanded_db_engine/compare.sh` | `benchmarks/example_rust_compare/expanded_db_engine/compare.sh: line 140: argv[@]: unbound variable` | **1** | **Nonzero exit — see below.** The prescribed `sh` interpreter is bash 3.2, which errors when `"${argv[@]}"` is expanded under `set -u` for a case with no `.args` file. Only two cases (`kv_store`, `table_query`) printed `MATCH` before the aborted expansion. |

### Note on the nonzero exit (suite 4)

This is an **interpreter compatibility failure, not a stdout/exit mismatch**:

- `expanded_db_engine/compare.sh` declares `#!/usr/bin/env bash` and its own
  `RESULTS.md` documents the invocation as
  `bash benchmarks/example_rust_compare/expanded_db_engine/compare.sh`.
- `sh` on this host resolves to `/bin/sh`, GNU bash 3.2.57. Under `set -u`,
  bash 3.2 treats the expansion of an empty array (`argv[@]`, populated only
  when a `<name>.args` file exists) as an unbound variable and aborts at line
  140. Bash 4.3+ — including the documented invocation — does not.
- Diagnostic re-run with the documented interpreter
  (`/opt/homebrew/bin/bash` 5.3.15, same restricted environment, same 300 s
  cap) produced 10/10 `EXPANDED_COMPARE ... MATCH stdout byte-identical,
  exit=0 (both)` lines and **exit 0**.
- Therefore the honest status is: **prescribed `sh` run = exit 1 (aborted after
  2 of 10 cases); documented `bash` run = exit 0 (10/10 MATCH).** No source
  change was made; resolving this is a decision for the owning agent (e.g.
  require `bash`, or make the array expansion `set -u`-safe).

### Count discrepancy: observed 110 vs brief's 135

The task brief cites "135 adversarial cases". The suite's own authoritative
counter line in this run is `ADV_EXPANDED_TOTAL 110 110 0 0`, and
`tests/expanded_examples_adversarial/RESULTS.md` likewise records 110 as the
final count (34 CLI + 26 games + 25 database + 13 engine + 12 safety = 110).
The 135 figure is therefore stale or aspirational; this document reports the
**observed 110** and does not fabricate the difference. Regardless of the
number, adversarial cases are evidence, not proof (see Non-claims).

## Catalogue — expanded examples by category

The aggregate runner reports 20 total units (`4+3+3+3+3+4`). These correspond
to **19 distinct source programs**: the `SNAKE_EXAMPLES` total of 3 counts three
deterministic CLI scenarios (growth / wall / self) exercised over two programs,
`snake_core` and `snake_cli`. Every item is a deterministic teaching model;
"database" and "engine" name the modelled shape, not a production system.

### CLI — `CLI_EXAMPLES` (`cli/run.sh`) — 4

| Example | Purpose |
| --- | --- |
| `cli/calculator.cp` | Bounded integer expression evaluator (`+ - * /`, parentheses); refuses divide-by-zero and overflow via `error *`, with `vector`, `string`, `print`/`scan` and a strict-arithmetic division helper. |
| `cli/todo.cp` | In-memory task list with `add/list/done/remove/save/load`; checked file records, bounded title table, `string::field` parsing and `vector::at_checked`. |
| `cli/file_tools.cp` | Deterministic `size`/`copy`/`cmp` utilities over fixed paths; a one-byte-over read is refused rather than truncated (`file::read` + `error *`). |
| `cli/text_tools.cp` | `words`/`lines`/`rev`/`find` over a fixed input file; checked `read_line`, `string::find` and `vector` length tracking. |

### Games — cores — `GAME_CORES` (`games/cores/run.sh`) — 3

| Example | Purpose |
| --- | --- |
| `games/cores/game2048.cp` | Fixed-seed 4x4 2048 move/merge logic with a bounded CLI driver; checked index reads. |
| `games/cores/maze.cp` | Fixed-layout maze with pure wall-collision and goal rules over a bounded move sequence. |
| `games/cores/tictactoe.cp` | Deterministic 3x3 tic-tac-toe core with two fixed cell-sequence players; refuses occupied/out-of-range moves. |

### Games — snake — `SNAKE_EXAMPLES` (`games/snake/run.sh`) — 2 programs / 3 scenarios

| Example | Purpose |
| --- | --- |
| `games/snake/snake_core.cp` | Fixed-seed 20x20 Snake logic core held in one struct and rendered into a caller buffer; `vector` body, fixed-seed LCG, `error` refusals, C-name lowering. |
| `games/snake/snake_cli.cp` | CLI driver embedding the core; emits INIT/board/`SCENARIO` transcripts for the three fixed cases `growth`, `wall`, `self` — the three units counted by `SNAKE_EXAMPLES 3 3 0`. |

### Database — `DB_EXAMPLES` (`database/run.sh`) — 3

| Example | Purpose |
| --- | --- |
| `database/kv_store.cp` | Teaching key/value store: hostile keys, oversized values, duplicate and missing keys become recoverable refusals with stable codes/categories; owned `string`, `vector::at_checked`, `reserve`-before-commit. |
| `database/table_query.cp` | Tiny in-memory table with checked predicates, aggregates and duplicate-id detection; `vector` rows + `intmap`, checked field parsing, SUM-overflow refusal. |
| `database/safety_demo.cp` | Deliberately hostile fixed-width records; every operation must be REFUSED with error details and intact canaries. |

### Engine — `ENGINE_EXAMPLES` (`engine/run.sh`) — 3

| Example | Purpose |
| --- | --- |
| `engine/ecs.cp` | Minimal fixed-seed Entity-Component-System model with spawn/tick/sweep; `vector` ids + `intmap` component maps, `error *` for unsatisfiable lookups. |
| `engine/engine_loop.cp` | Fixed-timestep game-loop simulator driven by explicit `N/L/R/F` tokens; explicit tick counter, no wall clock. |
| `engine/resource_cache.cp` | Deterministic refcounting resource manager keyed by string id; refuses double-release and use-after-release via state/generation metadata. |

### Safety — `SAFETY_DEMOS` (`safety/run.sh`) — 4

| Example | Purpose |
| --- | --- |
| `safety/memcpy_s_demo.cp` | In a forked child, unguarded `memcpy` one-over corrupts a canary; locally `cplus_memcpy_s` refuses the same write (links `libcplus_safecrt.a` + `libcplus_hardened.a`). |
| `safety/bounds_demo.cp` | Bounded query predicates from `stdlib/memory.hp`: exact-fit, one-over, interior pointer with explicit base, stale-generation key; metadata only, nothing dereferenced. |
| `safety/lifetime_demo.cp` | Valid borrow, stale-token refusal (metadata only) and double-release refusal from `stdlib/lifetime_adapt.hp`. |
| `safety/sync_demo.c` | C99 checked destroy/join wrappers from `build/libc/libcplus_hardened.a` (the one non-C++ source in the campaign). |

**Out of scope at verification time:** a sibling agent was still adding
`examples/expanded/games_2/` (`breakout.cp`, `invaders.cp`, `pong.cp`) with no
`run.sh` at the moment of this run, so those sources were not discovered by
`examples/expanded/run_all.sh` and are not counted above. `cli/_probe.cp` and
`cli/_probe2.cp` are scratch probes, not catalogue entries.

## Non-claims

- **Teaching models, not production.** Every example uses fixed seeds, bounded
  input and an explicit argv/stdin transcript. No wall clock, interactive loop,
  network, persistence, SQL, transaction log or hidden global state is claimed.
  A group named "database" or "engine" describes the model it demonstrates.
- **`MATCH` means stdout/exit parity only.** For the fixed deterministic argv
  and empty stdin exercised by the comparison suites, `MATCH` asserts that the
  captured stdout bytes and process exit status were identical. It says
  nothing about untested inputs, longer executions, stderr, performance, or
  the strength of the underlying mechanisms. It is not an equivalence proof
  between C+ and Rust.
- **110 adversarial cases are evidence, not proof.** (The brief cites 135; the
  observed count is 110 — see above.) All observed hostile cases were refused
  without a crash or unexpected acceptance; that is a record of the exercised
  inputs only, not a guarantee for all inputs.
- **No 100% memory-safety claim.** The safety demos are runtime refusal
  evidence, observed rather than statically asserted; a matching transcript
  does not establish safety in general.
- **No cross-platform claim.** All evidence was produced on macOS arm64 with
  Apple clang pinned to `/usr/bin/clang`. No other OS or architecture is
  claimed.
- **Nonzero exits are reported, never re-labelled.** Suite 4 exited 1 under the
  prescribed `sh` invocation; that result is stated above rather than hidden
  behind the passing `bash` diagnostic. `BLOCKED`/`PENDING` are inconclusive,
  never counted as a pass.
