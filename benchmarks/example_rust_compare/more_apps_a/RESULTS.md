# games_4 / apps_2 / systems_2 — C+ vs safe-Rust stdout/exit comparison

This directory holds one standalone **safe-Rust** model (`.rs`, no `unsafe`, no
FFI, no external crates) for each of the eleven NEW expanded examples plus a
single `compare.sh` that builds the original C+ source and the Rust model,
runs both with **identical fixed argv inputs** under a 30-second timeout, and
compares **stdout bytes and process exit status**.

All numbers and result lines below come from actual runs, not from synthesis.
The captured stdout/stderr of the final run are in `out/final.stdout` and
`out/final.stderr`.

## Toolchains (exact, from the captured run)

```
CMD: rustc --version
rustc 1.100.0 (0ef1bc386 2026-11-09)

CMD: ./build/c+ --version 2>&1 | head -1
C+ 0.1.0

CMD: /usr/bin/clang --version | head -2
Apple clang version 16.0.0 (clang-1600.0.26.6)
Target: arm64-apple-darwin27.2.0

CMD: timeout --help 2>&1 | head -1
Usage: timeout [-f | --foreground] [-k time | --kill-after time] [-p | --preserve-status] [-s signal | --signal signal]  [-v | --verbose] <duration> <command> [arg ...]

CMD: uname -m
arm64
```

Repository path for the run:
`/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib`, branch `agent/modern-stdlib`.

## Reproduction

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
sh benchmarks/example_rust_compare/more_apps_a/compare.sh
```

`compare.sh` writes only `MORE_APPS_COMPARE ...` lines to stdout; build
commands, hashes and one final `SUMMARY different=N inconclusive(BLOCKED+PENDING)=N`
line go to stderr. Build/run artifacts are kept in
`benchmarks/example_rust_compare/more_apps_a/out/`
(per-example `<name>.c.build.log`, `<name>.rs.build.log`,
`<name>.cases.c.stdout`, `<name>.cases.r.stdout`,
`<name>.cases.c.rc`, `<name>.cases.r.rc`).

## Exact build and run commands used by `compare.sh`

C+ (all eleven, cwd = repository root so `stdlib/*.hp` includes resolve):

```sh
timeout 30 ./build/c+ build --strict --sanitize=none -I. \
    -o benchmarks/example_rust_compare/more_apps_a/out/c_<name> \
    examples/expanded/<games_4|apps_2|systems_2>/<name>.cp
```

`json_query` additionally needs the vendored yyjson header and library (same
as its own build comment), so its actual command is:

```sh
timeout 30 ./build/c+ build --strict --sanitize=none -I. \
    -Ithird_party/yyjson -Lbuild/ext -lyyjson \
    -o benchmarks/example_rust_compare/more_apps_a/out/c_json_query \
    examples/expanded/apps_2/json_query.cp
```

Rust (all eleven):

```sh
timeout 30 rustc -O --edition 2021 <name>.rs \
    -o benchmarks/example_rust_compare/more_apps_a/out/r_<name>
```

Run (each fixed case; identical argv and empty stdin for both sides):

```sh
timeout 30 <binary> <fixed argv>   # stdout appended, exit code recorded
```

Comparison: `cmp -s <c.stdout> <r.stdout> && cmp -s <c.rc> <r.rc>`.

## Fixed inputs (identical for both sides)

| Example | Fixed invocations (cases concatenated for the comparison) |
|---|---|
| `connect4` | no args; `3 3 4 4 5 5 6 6`; `33445566`; `9` (E_RANGE); `0 0 0 0 0 0 0` (full-column refusal) |
| `checkers` | no args; `2130 5041 1021 4132 2334 3223 0110 2301`; compact `21305041102141322334322301102301`; `0000` (illegal) |
| `chesslite` | no args; `0120 6343 1323 6151 0625 6757 2544 6656 1434 4334`; compact `0120634313236151062567572544665614344334`; `9999` (E_RANGE) |
| `vault` | `add github s3cr3t -- add mail hunter2 -- list -- get github -- delete mail -- list -- get mail`; `add a 1 -- add a 2 -- get nope -- badcmd --` |
| `json_query` | `{"user":{"name":"Ada","age":36},"items":[{"tag":"x"}]} user.name`; `... items.0.tag`; `{"a":1} b` (missing-key); `{"a":1} a.b` (bad-path); `notjson a` (parse) |
| `csv_tool` | `<fixture> 1 sum`; `<fixture> 2 avg`; `<fixture> 5 sum` (out-of-range); `<fixture> 1 badmode` |
| `matrix` | `add 2 2 1,2,3,4 2 2 5,6,7,8`; `multiply 2 3 1,2,3,4,5,6 3 2 7,8,9,10,11,12`; `determinant 2 2 1,2,3,4`; `determinant 3 3 1,2,3,4,5,6,7,8,9` |
| `lru_cache` | `cap 2 -- put a 1 -- put b 2 -- get a -- put c 3 -- dump -- expect b 2 -- expect a 1`; `cap 0 -- cap 99 -- get nope -- dump`; no args |
| `scheduler` | `capacity 4 -- arrive A 1 2 -- arrive B 0 3 -- arrive C 1 1 -- run 5 -- dump -- expect B 1`; `capacity 0 -- arrive X 2 1 -- tick -- dump`; no args |
| `event_bus` | `topic news -- sub news alice -- sub news bob -- pub news hello -- unsub news alice -- pub news bye -- dump -- expect news bob`; `topic news -- sub news alice -- sub news alice -- pub nope x -- dump`; no args |
| `ring_buffer` | `cap 4 -- push 1 2 3 -- pop 2 -- push 4 5 6 -- peek 3 -- expect 3 3 4 5 -- dump`; `cap 0 -- push 1 -- pop 1 -- dump`; no args |

The CSV fixture is rewritten with fixed bytes by `compare.sh` on every run to
`out/fixtures/csv_basic.txt`:

```
1,10,100
2,20,200
3,30,300
```

`stdin` is empty for both sides.

## Captured result lines (verbatim stdout of `compare.sh`)

```
MORE_APPS_COMPARE connect4 MATCH stdout+exit identical for 5 fixed case(s)
MORE_APPS_COMPARE checkers MATCH stdout+exit identical for 4 fixed case(s)
MORE_APPS_COMPARE chesslite MATCH stdout+exit identical for 4 fixed case(s)
MORE_APPS_COMPARE vault MATCH stdout+exit identical for 2 fixed case(s)
MORE_APPS_COMPARE json_query MATCH stdout+exit identical for 5 fixed case(s)
MORE_APPS_COMPARE csv_tool MATCH stdout+exit identical for 4 fixed case(s)
MORE_APPS_COMPARE matrix MATCH stdout+exit identical for 4 fixed case(s)
MORE_APPS_COMPARE lru_cache MATCH stdout+exit identical for 3 fixed case(s)
MORE_APPS_COMPARE scheduler MATCH stdout+exit identical for 3 fixed case(s)
MORE_APPS_COMPARE event_bus MATCH stdout+exit identical for 3 fixed case(s)
MORE_APPS_COMPARE ring_buffer MATCH stdout+exit identical for 3 fixed case(s)
```

Script exit status: **0**. Final stderr summary line:
`SUMMARY different=0 inconclusive(BLOCKED+PENDING)=0`.

## Per-example table

| Example | C+ source | Rust model | stdout bytes (C / Rust) | Exit codes (C / Rust) | SHA-256 (both) | Result |
|---|---|---|---:|---|---|---|
| `connect4` | `examples/expanded/games_4/connect4.cp` | `connect4.rs` | 3588 / 3588 | 0,0,0,2,2 | `166008b6f636cd5fec752e5d82e3a16f2bdd7b546255f53c34dce162d70cbecc` | **MATCH** |
| `checkers` | `examples/expanded/games_4/checkers.cp` | `checkers.rs` | 4662 / 4662 | 0,0,0,2 | `e348c69f882213de0ca42b87b43cfb5a0a6f6a78b5963cb81ee41193045c8038` | **MATCH** |
| `chesslite` | `examples/expanded/games_4/chesslite.cp` | `chesslite.rs` | 5567 / 5567 | 0,0,0,2 | `dc4fd41f22f337a073ec1ab1ad4c26104f769acd2088321e913ae1cee4f42119` | **MATCH** |
| `vault` | `examples/expanded/apps_2/vault.cp` | `vault.rs` | 271 / 271 | 1,1 | `8d54a6037ed25251ec2779565cc8ac87e4f6800676e21a7003ff1f60f0ef4804` | **MATCH** |
| `json_query` | `examples/expanded/apps_2/json_query.cp` | `json_query.rs` | 110 / 110 | 0,0,1,1,1 | `f0f5eb3440182571106cd02ffaa27aa24d8bff63977fc9934b41c2e4ab1a64b1` | **MATCH** |
| `csv_tool` | `examples/expanded/apps_2/csv_tool.cp` | `csv_tool.rs` | 98 / 98 | 0,0,1,1 | `9a31d99fa6aeae318a50347a014aa6d25a92a4b93ce23f9b2db4fe3179951570` | **MATCH** |
| `matrix` | `examples/expanded/apps_2/matrix.cp` | `matrix.rs` | 158 / 158 | 0,0,0,0 | `1f55ac5051cde9434016518b8a5643a61c4d590585704873092e1e0d2e1c65c2` | **MATCH** |
| `lru_cache` | `examples/expanded/systems_2/lru_cache.cp` | `lru_cache.rs` | 537 / 537 | 1,0,0 | `fa3396bc99c26e86566913348db3927ced39ce294c4607bd39113f7d19b9f656` | **MATCH** |
| `scheduler` | `examples/expanded/systems_2/scheduler.cp` | `scheduler.rs` | 729 / 729 | 0,0,0 | `949e3cd72e2c4bc73da2aaa9930efc6c7643be8f36f865574a06b7044bd52410` | **MATCH** |
| `event_bus` | `examples/expanded/systems_2/event_bus.cp` | `event_bus.rs` | 695 / 695 | 0,0,0 | `742f858f21c3dded07f94986286ae73003230c0f26cda8e89b96b6e792006fcf` | **MATCH** |
| `ring_buffer` | `examples/expanded/systems_2/ring_buffer.cp` | `ring_buffer.rs` | 711 / 711 | 0,0,0 | `c74cbbd63be0a3b7907aa3dedb7fa287b27004244f3d15b2a0e56b6aaa55754c` | **MATCH** |

Byte counts are from `wc -c` on the captured combined-stdout files. The SHAs
are the combined stdout of all fixed cases; the C and Rust SHA-256 values are
identical for every example (full values are also in `out/final.stderr`).

## Totals

| MATCH | DIFFERENT | BLOCKED | PENDING | Total |
|---:|---:|---:|---:|---:|
| 11 | 0 | 0 | 0 | 11 |

Forty fixed case invocations were compared in total (5+4+4+2+5+4+4+3+3+3+3).
No case was untestable, so no `BLOCKED` result was needed. At the time of the
final run all eleven scoped C+ sources existed, so no `PENDING` result was
produced. These sources were produced by sibling agents in parallel; the
models were written against the versions present at final-run time (source
mtimes all precede the final run) and the final run rebuilt every binary from
those sources.

## Exit-status semantics (verified)

`compare.sh` exits nonzero **only on a real `DIFFERENT`**. `BLOCKED` and
`PENDING` are reported but never make the script fail. This was verified with
controlled runs using environment overrides (all isolated in directories under
this folder, no result files faked):

| Scenario | Command override | Observed exit | As required |
|---|---|---:|---|
| All MATCH (final run) | none | 0 | yes |
| All `BLOCKED` (`rustc` forced to fail) | `MA_RUSTC=false MA_OUT=$PWD/out/verify_blocked` | 0 | yes — inconclusive must not fail |
| All `DIFFERENT` (stub compiler emits `WRONG`) | `MA_RUSTC=$PWD/out/fake_rustc.sh MA_OUT=$PWD/out/verify_diff` | 11 (one per example) | yes — real DIFFERENT must fail |
| All `PENDING` (source paths still missing) | earlier run before the `.cp` suffix was added | 0 | yes — inconclusive must not fail |

The final, real run produced eleven `MATCH` lines, exit `0`, and
`SUMMARY different=0 inconclusive(BLOCKED+PENDING)=0`. No `MATCH` was
fabricated: every line came from an actual C+/Rust build and paired execution.

## `PENDING` behaviour and exact refresh command

`compare.sh` is deliberately safe about sources that are still being created
by sibling agents. If a scoped C+ source (or its Rust model) is absent at run
time, that example is reported as `PENDING` instead of being scored a `MATCH`:

```
MORE_APPS_COMPARE <name> PENDING source-not-yet-created <abs path>; refresh: sh <here>/compare.sh
```

Exact command to refresh after a missing source appears:

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
sh benchmarks/example_rust_compare/more_apps_a/compare.sh
```

No `MATCH` is ever fabricated for a missing source: the `PENDING` branch runs
before any build or comparison.

## How each C+ feature was modeled (and what differs)

- **`connect4` / `checkers` / `chesslite`** — the C+ guarded 2-D arrays become
  `[i32; N]` / `Vec<i32>` boards; every board read goes through a bounds-checked
  `tile()` helper. Compact-digit token parsing is reproduced exactly (including
  the differing compact rules: 0..6 digits for Connect Four, 4-digit 0..7 groups
  for checkers/chesslite). `error *` refusal records become stable printed
  `REFUSE` lines with the same 101/5 and 108/1 code/category pairs.
  Difference: C+ checked `vector`/`string`, `.hp` lowering and runtime checks
  have no direct Rust analogue; Rust uses `Vec`, slices and `Option`/`Result`.
- **`vault`** — fixed-capacity tables become an ordered `Vec<(String, String)>`
  used as the map so `list` keeps the C+ insertion order (a plain `HashMap`
  would randomize it). Length caps (256), refusal tokens and the "secrets only
  from `get`" rule are reproduced. Difference: no C+ `error *`, no checked
  `string::substr`; Rust `String` owns the data.
- **`json_query`** — yyjson is replaced by a small safe recursive-descent parser
  over `&str`; the depth-budget scan (container opens outside strings, cap 8)
  is reproduced byte-for-byte so `depth` vs `parse` refusals match. `print()`
  output, path segmentation, array indices and integer-only `get_int` are
  modeled. Difference: no yyjson value representation, no `json_view`/`error *`.
- **`csv_tool`** — `file::read_line` + `error *` become `std::fs::read` and
  checked `i64` arithmetic; the fixed fixture is rewritten each run. `strtol`
  full-string validation, column count and signed-sum overflow checks are
  reproduced. Difference: C+ fixed stack buffers and `string::field` are
  replaced by `str::split` and `Result`.
- **`matrix`** — the C+ 4x4 `long long` matrices become `Vec<Vec<i64>>`;
  `mat_checked_mul`/`add`/`sub`, the 3x3 rule and the 4x4 cofactor expansion are
  translated with `i128` intermediates so refusals follow the same MAT_SAFE
  limits without Rust overflow. The current `det <r> <c> <data>` signature is
  used. Difference: no C+ global arrays, no `error *`.
- **`lru_cache` / `scheduler` / `event_bus` / `ring_buffer`** — the C+
  `strmap` becomes `HashMap<String, i32>` for key/slot lookup while ordered
  state uses `Vec`/`VecDeque` (LRU order, subscriber lists, queues, ring bytes,
  op logs). Every hostile input is refused with the same category/code pairs
  (1/108, 2/101, 6/106, 4/107) and the same `print()` spacing. Difference:
  no C+ `deque`/`vector` checked accessors, no `.hp` header lowering, no
  `error *` object; Rust uses owned collections and explicit counters.
  For the vault and bus, an ordered `Vec`-backed map is used deliberately
  because the C+ output is insertion/slot-ordered.

Every `Rust` program contains **no `unsafe` blocks, no FFI, no raw pointers and
no external crates**. Each `.rs` compiles standalone with
`rustc -O --edition 2021`.

## Explicit non-equivalence statement

A `MATCH` here means only: **for the fixed, deterministic argv and empty stdin
used, the original C+ executable and the safe-Rust model produced the same
stdout bytes and the same process exit codes.** It does **not** mean the two
programs are language-, ABI-, implementation- or safety-equivalent. The Rust
files are idiomatic *models of observable behaviour*, not translations of the
C+ lowering pipeline: `.hp` header lowering, checked `vector`/`string`/`file`/
`strmap`/`deque` wrappers, runtime bounds checks, `error *` categories, C+ RAII
and the strict checker's refusals have no direct Rust syntax analogue and were
replaced with `Vec`/`String`/`Result`/`HashMap`, `impl` methods, iterators and
Rust's type-system safety. Memory layout, allocation strategy, overflow
diagnostics, error-object semantics, yyjson internals and compile-time
guarantees were **not** compared. No claim is made beyond the forty
stdout/exit observations above.
