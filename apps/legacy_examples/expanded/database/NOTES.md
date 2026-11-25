# Database examples — verification notes

## Scope and honesty statement

These three programs are **in-memory teaching examples, not a persistent
database engine**. There is no disk storage, no SQL, no transaction log, no
indexing beyond a small `intmap`, no concurrency and no durability guarantee.
All state is lost when the process exits.

What they *do* demonstrate is runtime safety through the checked C+ standard
library APIs:

- `stdlib/string` — owned buffers, `at()` / `append()` bounds checks, UTF-8
  validation, and explicit NULL argument refusal;
- `stdlib/vector` — `at_checked()` reads, `reserve()` before commit,
  `erase()` / `copy_from()` for mutation, no raw pointer arithmetic;
- `stdlib/collections` — `intmap` duplicate-id detection;
- `stdlib/error` — stable refusal codes (101 bounds, 108 unsupported) and
  stable categories/messages for every hostile input;
- overflow-safe integer addition that refuses **before** signed overflow is
  executed (no reliance on UB or on sanitizers; builds use `--sanitize=none`).

All inputs and commands are fixed in `run.sh`; output is deterministic.
No live game, network, filesystem or user-specific path is involved.

## Files

| file | purpose |
| --- | --- |
| `kv_store.cp` | in-memory key/value store: insert/get/update/delete/list, duplicate-key refusal, key/value length bounds, `expect_size` assertion |
| `table_query.cp` | in-memory table (id, grp, amt): row insert with duplicate-id refusal, equality/range queries, count/sum aggregates, overflow refusal on SUM, self-checking `expect_*` commands |
| `safety_demo.cp` | deliberately hostile operations that must all be REFUSED with error details and intact canaries |
| `run.sh` | builds and runs all three with fixed inputs, checks exit codes, stderr, summary lines and run-to-run determinism |

## Exact commands and results

All commands below were run from the repository root
`/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib` on branch
`agent/modern-stdlib`. Build output was empty; build exit code was 0 for each.

### Build (one command per example)

```sh
./build/c+ build examples/expanded/database/kv_store.cp --strict --sanitize=none -I. -o examples/expanded/database/results/kv_store
./build/c+ build examples/expanded/database/table_query.cp --strict --sanitize=none -I. -o examples/expanded/database/results/table_query
./build/c+ build examples/expanded/database/safety_demo.cp --strict --sanitize=none -I. -o examples/expanded/database/results/safety_demo
```

Result: no output, exit 0 in all three cases.

### `kv_store` (fixed argv sequence)

```sh
timeout 30 ./examples/expanded/database/results/kv_store \
  insert alpha one insert beta two insert alpha duplicate \
  get alpha get gamma update alpha ALPHA delete beta list expect_size 1
```

Exact stdout:

```
KV insert OK alpha
KV insert OK beta
KV insert REFUSED 108 invalid duplicate key
KV get alpha one
KV get REFUSED 108 invalid no such key
KV update OK alpha
KV delete beta OK
KV item 0 alpha ALPHA
KV list 1
KV expect_size 1 OK
KV_SUMMARY 9 7 2 0
```

Exit 0, stderr empty. Reading the same commands from stdin (one command per
line) also works via the built-in stdin fallback.

### `table_query` (fixed argv sequence)

```sh
timeout 30 ./examples/expanded/database/results/table_query \
  row 1 10 100 row 2 10 200 row 3 20 300 row 4 20 400 row 2 10 999 \
  eq grp 10 range amt 200 400 count grp 10 sum grp 10 \
  expect_count grp 10 2 expect_sum grp 10 300 \
  row 5 30 2147483647 row 6 30 1 range grp 30 30 sum grp 30 \
  expect_sum_refusal grp 30 101 check 6
```

Exact stdout:

```
TQ row 1 OK
TQ row 2 OK
TQ row 3 OK
TQ row 4 OK
TQ row 2 REFUSED 108 invalid duplicate id
TQ eq grp 10 2
TQ row 1 10 100
TQ row 2 10 200
TQ range amt 200 400 3
TQ row 2 10 200
TQ row 3 20 300
TQ row 4 20 400
TQ count grp 10 2
TQ sum grp 10 300
TQ expect_count grp 10 OK
TQ expect_sum grp 10 OK
TQ row 5 OK
TQ row 6 OK
TQ range grp 30 30 2
TQ row 5 30 2147483647
TQ row 6 30 1
TQ sum REFUSED 101 range sum overflow
TQ expect_sum_refusal grp 30 OK
TQ check 6 OK
TQ_SUMMARY 17 15 2 0
```

Exit 0, stderr empty. `sum grp 30` refuses because `2147483647 + 1` would
overflow `int`; the two rows themselves are valid data and remain in the table.

### `safety_demo` (no arguments)

```sh
timeout 30 ./examples/expanded/database/results/safety_demo
```

Exact stdout:

```
SAFETY oversized_key REFUSED 101
SAFETY oversized_key DETAIL range key length out of bounds
SAFETY oversized_key CANARY ok
SAFETY null_value REFUSED 108
SAFETY null_value DETAIL invalid null value
SAFETY null_value CANARY ok
SAFETY overflowing_sum REFUSED 101
SAFETY overflowing_sum DETAIL range sum overflow
SAFETY overflowing_sum CANARY ok
SAFETY duplicate_key REFUSED 108
SAFETY duplicate_key DETAIL invalid duplicate key
SAFETY duplicate_key CANARY ok
SAFETY delete_missing REFUSED 108
SAFETY delete_missing DETAIL invalid no such key
SAFETY delete_missing CANARY ok
SAFETY FINAL_VALUE dup one
SAFETY FINAL_CANARY ok
SAFETY FINAL_STORE 1
SAFETY_SUMMARY 5 5 5 0
```

Exit 0, stderr empty. Each `<op> REFUSED <code>` line is accompanied by a
`<category> <message>` detail line and a canary result; after all five hostile
operations the canary is still intact, the surviving `dup` record still reads
back as `one`, and the live record count is still 1.

### Aggregate runner

```sh
./examples/expanded/database/run.sh
```

Result: rebuilds each example, runs it twice under `timeout 30`, compares the
two stdout files with `cmp` (byte-identical in all three cases), verifies empty
stderr and the expected summary line, and finishes with:

```
DB_EXAMPLES 3 3 0
```

Exit 0. Logs and binaries are written to `examples/expanded/database/results/`.

## Refusal codes used

| code | name | meaning in these examples |
| --- | --- | --- |
| 101 | `CPLUS_EXIT_BOUNDS` | key/value length out of range, bad vector field access, empty/lo > hi range, SUM overflow |
| 108 | `CPLUS_EXIT_UNSUPPORTED` | NULL key/value, duplicate key/id, missing key on get/update/delete, unknown command |
| 107 | `CPLUS_EXIT_RESOURCE` | checked allocator / reserve failure (not triggered by the fixed inputs) |

No sanitizer was enabled (`--sanitize=none`); the refusals above come from
explicit checks in the C+ code and the checked stdlib, not from a sanitizer
report. A nonzero exit code is used only for unhandled failures (e.g. a failed
`expect_*` assertion, build error, empty build, or nonzero runtime exit).
