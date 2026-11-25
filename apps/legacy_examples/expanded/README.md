# Expanded C+ standard-library examples

This directory holds the **expanded example campaign**: small, self-contained
programs that exercise the checked C+ standard library in larger, more
recognisable shapes than `demos/`.

> **These are deterministic teaching models, not production applications,
> games, databases or engines.** Every example uses fixed seeds, bounded input
> and an explicit argv/stdin transcript so that the same invocation yields
> byte-identical output on every run. There is no wall clock, no interactive
> terminal loop, no network and no hidden global state drift. A group named
> "database" or "engine" describes the *model* it demonstrates, not a claim to
> be a real database or game engine. See [Non-claims](#non-claims) below.

## Run all groups

```sh
sh examples/expanded/run_all.sh
```

The aggregate runner discovers every `<group>/run.sh` and
`<group>/<subgroup>/run.sh` below this directory, runs each from the repository
root under `timeout -k 10 300` with a restricted `PATH` and
`CPLUS_CLANG=/usr/bin/clang`, captures the **last** final-report line from each
group, prints a table, and exits non-zero on any real failure. It continues
after failures and never masks a failure with a success override. Per-group
logs land in `build/expanded_examples/<runner>.log`; the verbatim final lines
are saved to `build/expanded_examples/final_lines.txt`.

### Final-line contract (for group authors)

Each `run.sh` must print, as its last matching line, exactly one line beginning
with its token:

| Runner | Token |
| --- | --- |
| `cli/run.sh` | `CLI_EXAMPLES` |
| `games/snake/run.sh` | `SNAKE_EXAMPLES` |
| `games/cores/run.sh` | `GAME_CORES` |
| `database/run.sh` | `DB_EXAMPLES` |
| `engine/run.sh` | `ENGINE_EXAMPLES` |
| `safety/run.sh` | `SAFETY_DEMOS` |

Recommended shape (the positional form is what the game groups emit):

```
TOKEN <total> <passed> <failed>
```

The aggregate parser also accepts `TOKEN total=<n> pass=<n> fail=<n>
[blocked=<n>]` and a bare status word. A third positional count greater than
zero, `fail=<n>` with `n > 0`, or the words `FAIL`, `MISMATCH`, `ERROR`, `BUILD_FAIL`,
and a non-zero exit status are real failures. `BLOCKED` / `blocked=<n>` is
reported but is not a hard failure, matching the repository evidence
convention. Group-specific detail belongs in that group's `NOTES.md`.

## Common build convention

All examples are built from the repository root with the supported driver in
strict mode. The aggregate runner pins the backend to Apple clang; individual
build commands may spell the driver as `./build/c+` or as `c+` on `PATH`:

```sh
./build/c+ build <source> --strict --sanitize=none -I. -o <output>
```

## Catalogue by category

### CLI — `CLI_EXAMPLES` (`cli/run.sh`)

| Example | One-line purpose | Build | C+ stdlib APIs demonstrated |
| --- | --- | --- | --- |
| `cli/calculator.cp` | Deterministic bounded integer expression evaluator (`+ - * /`, parentheses), refusing divide-by-zero and overflow. | `./build/c+ build examples/expanded/cli/calculator.cp --strict --sanitize=none -I. -o examples/expanded/cli/.run/calculator` | `error *` / `error_fail` (`stdlib/error.hp`); `vector` (`stdlib/vector.hp`); `string` (`stdlib/string.hp`); `print`/`scan` (`stdlib/io.hp`); strict-arithmetic-safe division helper |
| `cli/todo.cp` | In-memory task list with chainable `add/list/done/remove/save/load` commands and checked file records. | `./build/c+ build examples/expanded/cli/todo.cp --strict --sanitize=none -I. -o examples/expanded/cli/.run/todo` | `vector` + `at_checked`; `string::substr` and `string::field` (checked parsing); `stdlib/file.hp` text I/O; `error *` refusals; bounded title table |
| `cli/file_tools.cp` | Deterministic `size`/`copy`/`cmp` file utilities that refuse a one-byte-over read instead of truncating. | `./build/c+ build examples/expanded/cli/file_tools.cp --strict --sanitize=none -I. -o examples/expanded/cli/.run/file_tools` | `stdlib/file.hp` checked `file::read`; `error *` bounds refusal; `vector` bounded buffer |
| `cli/text_tools.cp` | Deterministic `words`/`lines`/`rev`/`find` text utilities over a fixed input path. | `./build/c+ build examples/expanded/cli/text_tools.cp --strict --sanitize=none -I. -o examples/expanded/cli/.run/text_tools` | `stdlib/file.hp` checked `read_line`; `string::find`; `vector` + `at_checked` for lengths; `error *` out-parameter |

### Games — Snake — `SNAKE_EXAMPLES` (`games/snake/run.sh`)

| Example | One-line purpose | Build | C+ stdlib APIs demonstrated |
| --- | --- | --- | --- |
| `games/snake/snake_core.cp` | Fixed-seed 20x20 Snake *logic core*; state is one `SnakeCore` struct, rendered into a caller buffer. | `./build/c+ build examples/expanded/games/snake/snake_core.cp --strict --sanitize=none -I. -o examples/expanded/games/snake/.build/snake_core` | `vector` snake body (`stdlib/vector.hp`); `error` refusals (`stdlib/error.hp`); C-name lowering (`vector_ctor`, `vector_insert`, `error_set_category`); fixed-seed LCG |
| `games/snake/snake_cli.cp` | Deterministic CLI driver that embeds the core and prints INIT / board / SCENARIO transcripts. | `./build/c+ build examples/expanded/games/snake/snake_cli.cp --strict --sanitize=none -I. -o examples/expanded/games/snake/.build/snake_cli` | Core embedding via `#include`; bounded direction decoding; checked vector reads; `error` status propagation |

### Games — Cores — `GAME_CORES` (`games/cores/run.sh`)

| Example | One-line purpose | Build | C+ stdlib APIs demonstrated |
| --- | --- | --- | --- |
| `games/cores/game2048.cp` | Deterministic fixed-seed 4x4 2048 move/merge logic with a bounded CLI driver. | `./build/c+ build examples/expanded/games/cores/game2048.cp --strict --sanitize=none -I. -o build/game2048_core` | `vector`, `string`, `error` (`stdlib/vector.hp`, `stdlib/string.hp`, `stdlib/error.hp`); checked index reads; bounded argv/stdin parsing |
| `games/cores/maze.cp` | Deterministic fixed-layout maze with pure wall-collision and goal rules. | `./build/c+ build examples/expanded/games/cores/maze.cp --strict --sanitize=none -I. -o build/maze_core` | `vector`, `string`, `error`; pure functions over fixed state; bounded move sequence |
| `games/cores/tictactoe.cp` | Deterministic 3x3 tic-tac-toe core with two fixed cell-sequence players. | `./build/c+ build examples/expanded/games/cores/tictactoe.cp --strict --sanitize=none -I. -o build/tictactoe_core` | `vector`, `string`, `error`; illegal-move refusal (occupied cell / out-of-range index) |

### Database — `DB_EXAMPLES` (`database/run.sh`)

| Example | One-line purpose | Build | C+ stdlib APIs demonstrated |
| --- | --- | --- | --- |
| `database/kv_store.cp` | Teaching key/value store that turns hostile keys, oversized values, duplicate keys and missing keys into *recoverable refusals*. | `./build/c+ build examples/expanded/database/kv_store.cp --strict --sanitize=none -I. -o examples/expanded/database/kv_store` | Owned `string` with checked `at()`/`append()`; `vector` with `at_checked()`, `reserve()` before commit, and `erase()`; `error_fail_cat` with stable codes/categories (`stdlib/error.hp`) |
| `database/table_query.cp` | Tiny in-memory table with checked predicates, aggregates and duplicate-id detection. | `./build/c+ build examples/expanded/database/table_query.cp --strict --sanitize=none -I. -o examples/expanded/database/table_query` | `vector` rows with `at_checked`; `intmap` duplicate detection (`stdlib/collections.hp`); checked `string` field parsing; `error *` refusal for bad predicates and SUM overflow |

**Not a database engine:** no persistence, no SQL, no transaction log, no
concurrency. It is a model of how checked containers and recoverable errors
behave under bad input.

### Engine — `ENGINE_EXAMPLES` (`engine/run.sh`)

| Example | One-line purpose | Build | C+ stdlib APIs demonstrated |
| --- | --- | --- | --- |
| `engine/ecs.cp` | Minimal fixed-seed Entity-Component-System model with spawn / tick / sweep phases. | `./build/c+ build examples/expanded/engine/ecs.cp --strict --sanitize=none -I. -o examples/expanded/engine/.build/ecs` | `vector` ids + `intmap` component maps (`stdlib/collections.hp`); `vector_at_checked`; `error *` for unsatisfiable lookups; fixed-seed RNG |
| `engine/engine_loop.cp` | Fixed-timestep game-loop simulator driven by explicit `N/L/R/F` input tokens. | `./build/c+ build examples/expanded/engine/engine_loop.cp --strict --sanitize=none -I. -o examples/expanded/engine/.build/engine_loop` | `vector` targets/state; `error *`; bounded tick/input decoding; explicit tick counter (no wall clock) |
| `engine/resource_cache.cp` | Deterministic refcounting resource manager by string id that refuses double-release and use-after-release. | `./build/c+ build examples/expanded/engine/resource_cache.cp --strict --sanitize=none -I. -o examples/expanded/engine/.build/resource_cache` | `string` ids; `intmap` / map handles (`stdlib/collections.hp`); state+generation metadata; recoverable `error *` refusal |

**Not a game engine:** a reproducible state machine used to show component
storage and error reporting.

### Safety — `SAFETY_DEMOS` (`safety/run.sh`)

| Example | One-line purpose | Build | C+ stdlib APIs demonstrated |
| --- | --- | --- | --- |
| `safety/memcpy_s_demo.cp` | Raw `memcpy` corrupts a canary in a forked child; checked `cplus_memcpy_s` refuses the same one-over write. | `./build/c+ build examples/expanded/safety/memcpy_s_demo.cp --strict --sanitize=none -I. -o examples/expanded/safety/memcpy_s_demo -- build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a` | `cplus_memcpy_s` (`stdlib/safe/safe_memory.h`); fork/waitpid isolation; runtime refusal evidence |
| `safety/bounds_demo.cp` | Bounded query predicates: exact-fit, one-over, interior pointer with explicit base, and stale-generation key. | `./build/c+ build examples/expanded/safety/bounds_demo.cp --strict --sanitize=none -I. -o examples/expanded/safety/bounds_demo` | `stdlib/memory.hp` metadata queries; `error *`; non-wrapping range check; nothing dereferenced |

These are **runtime refusal evidence, not a proof of memory safety**; every
outcome is observed, not statically asserted. The group's `run.sh` was still
**PENDING** when this catalogue was written — see `RESULTS.md` and
`examples/expanded/safety/NOTES.md` once it lands. To refresh:

```sh
find examples/expanded/safety -type f \( -name '*.cp' -o -name '*.c' \) | sort
sh examples/expanded/safety/run.sh
```

## Rust comparison mirrors

Two sibling-owned directories mirror selected groups for safe-Rust
comparisons; they are **not** run by `examples/expanded/run_all.sh`:

- `benchmarks/example_rust_compare/expanded_cli_games/compare.sh`
- `benchmarks/example_rust_compare/expanded_db_engine/compare.sh`

Run the whole comparison campaign with:

```sh
sh benchmarks/example_rust_compare/run_all.sh
```

That runner has its own `EXAMPLE_COMPARE <name> MATCH|DIFFERENT|BLOCKED <reason>`
protocol and its own `RESULTS.md`; see `benchmarks/example_rust_compare/ALL_EXAMPLES.md`.

## Non-claims

- These programs are **deterministic teaching models**, not production apps,
  games, databases or engines. Fixed inputs and fixed seeds are a teaching
  device, not a feature guarantee.
- **Matching output is not equivalent guarantees.** Identical stdout/stderr for
  the exercised inputs says nothing about untested inputs, longer executions,
  or the strength of the underlying safety mechanisms.
- No **Rust-equivalence** claim is made by this directory, and no **100%
  safety** claim is made for C+. C+ combines compiler analysis with runtime
  checks; Rust uses compile-time ownership/borrowing rules plus language
  runtime checks. The mechanisms differ even when a transcript matches.
- `BLOCKED` is reported, never counted as a pass. A group that has not landed
  is recorded as `PENDING` with an exact reproduction command, never fabricated.
- Evidence recorded in `RESULTS.md` was produced on **macOS arm64 only** with
  Apple clang pinned to `/usr/bin/clang`. No other OS or architecture is
  claimed.
