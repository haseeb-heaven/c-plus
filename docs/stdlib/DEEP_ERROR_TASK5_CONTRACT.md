# Deep task-5 contract: `stdlib/error.hp`

Per-family evidence for `tests/stdlib/deep_error_task5.cp`. This suite covers
only the genuinely uncovered gaps left by `deep_error_vector.cp` (which already
pins 255/256/288-byte truncation, reset/set, NULL free helpers, category mapping
for every exit code, status mapping and category names).

Status: **no defect found**. `stdlib/error.hp` was not modified.

## Scope and ownership

| Item | Path |
| --- | --- |
| Test | `tests/stdlib/deep_error_task5.cp` |
| Contract | `docs/stdlib/DEEP_ERROR_TASK5_CONTRACT.md` (this file) |
| Header under test | `stdlib/error.hp` (untouched) |
| Other files created | none |

The test is self-checking, has no `.stdin`/`.expected` file, and returns `0`
only when every check passes. Each check prints `<tag> <0|1>`; the final line is
`ERROR_TASK5 <checks> <failures>`. A documented SKIP prints `<tag> 1` followed
by `SKIP_REASON <tag> <reason>`.

## Scenarios

| Tag | What is asserted | Result |
| --- | --- | --- |
| `err_bound_0` | empty message: `strlen(what())==0`, `text[255]=='\0'`, state OK/IO | pass |
| `err_bound_1` | 1-byte message copied exactly, NUL at index 1 and 255 | pass |
| `err_bound_254` | 254-byte message copied exactly (`strlen==254`) | pass |
| `err_bound_255` | 255-byte message fits exactly (`strlen==255`), `text[255]=='\0'` | pass |
| `err_bound_256` | 256-byte input truncates to a 255-byte prefix, `text[255]=='\0'` | pass |
| `err_bound_257` | 257-byte input truncates to a 255-byte prefix, NUL terminated | pass |
| `err_no_stale_tail` | 255-byte `Q` message followed by `"s"` reads back exactly `"s"`, no stale tail | pass |
| `err_null_callback` | `stdlib_set_error_callback(NULL)` then `error_fail`: no crash, error recorded | pass |
| `err_reentrant_no_recursion` | callback calls `error_fail` on a global target: callback runs **once**, outer record wins (`"outer"`, IO/109) | pass |
| `err_reentrant_no_latch` | a second failure invokes the callback again: no stuck `reporting` latch | pass |
| `err_reentrant_telemetry` | `api_failures==4`, `alloc_failures==2`, `invalid_ops==1` (two outer notes + two suppressed nested notes) | pass |
| `err_1000_cycles` | 1000× `error_reset`+`error_fail(IO)`: `api_failures==1000`, `invalid_ops==1000`, final state IO/109 | pass |
| `err_1000_reset_clean` | final reset restores OK/OK/empty message | pass |
| `err_copy_state` | `error b = a` copies failed/category/code/message | pass |
| `err_copy_independent` | mutating the copy leaves the original unchanged (independent buffers) | pass |
| `err_move_state` | `cplus_move(a)` transfers state and zeroes the source (`ok()==1`) | pass |
| `err_fresh_category_ok` | a fresh `error` reports `STDLIB_CATEGORY_OK` | pass |
| `err_invalid_category_code` | `error_fail_cat(999, 7777, ...)` stores category 999 and code 7777 verbatim | pass |
| `err_unknown_name` | `stdlib_category_name(999)` and `(-5)` both return `"unknown"` | pass |
| `err_negative_category` | negative category −5 and code −1 are stored verbatim, no crash | pass |
| `err_invalid_exit_code` | `error_fail((cplus_exit_code)4242, ...)` uses the INVALID category by default and stores code 4242 | pass |
| `err_code_name_mapping` | **SKIP** — no exit-code-to-name API exists | — |

### Reentrancy observation (not a defect)

`stdlib_note()` calls the callback while its `static __thread int reporting`
guard is set. A nested `error_fail` therefore still increments telemetry
counters but does **not** re-invoke the callback. Because `error_fail_cat()`
writes the outer error fields *after* `stdlib_note()` returns, the outer record
overwrites the nested one. Both behaviours are asserted; neither is a latch bug.

### Copy/move observation

`error` is a plain `class` (no owned resource field), so C+ permits both copy
construction and `cplus_move()`. Copy produces an independent value; move zeroes
the source. Both were verified with a one-shot `c+ check`/build before being
pinned here. This differs from owning classes such as `json`/`file`, whose
implicit copies the compiler rejects.

## Exact stdout (byte-identical in all build modes)

```
err_bound_0 1
err_bound_1 1
err_bound_254 1
err_bound_255 1
err_bound_256 1
err_bound_257 1
err_no_stale_tail 1
err_null_callback 1
err_reentrant_no_recursion 1
err_reentrant_no_latch 1
err_reentrant_telemetry 1
err_1000_cycles 1
err_1000_reset_clean 1
err_copy_state 1
err_copy_independent 1
err_move_state 1
err_fresh_category_ok 1
err_invalid_category_code 1
err_unknown_name 1
err_negative_category 1
err_invalid_exit_code 1
err_code_name_mapping 1
SKIP_REASON err_code_name_mapping stdlib/error.hp exposes no exit-code-to-name function; only stdlib_category_name() with an "unknown" fallback, covered by err_unknown_name and by DEEP_ERROR_VECTOR_CONTRACT.md E4.
ERROR_TASK5 22 0
```

stderr is empty; exit code is `0`.

## Skips

- `err_code_name_mapping` — `stdlib/error.hp` has no function mapping a
  `cplus_exit_code` to a string; the only name helper is
  `stdlib_category_name()`, whose `"unknown"` fallback is already exercised.

## Verification commands

From the repository root (checked-in `build/c+` driver):

```sh
./build/c+ build --strict --sanitize=none tests/stdlib/deep_error_task5.cp \
  -o build/stdlib-tests/deep_error_task5.strict_none
timeout 30 ./build/stdlib-tests/deep_error_task5.strict_none < /dev/null

./build/c+ build tests/stdlib/deep_error_task5.cp \
  -o build/stdlib-tests/deep_error_task5.default
timeout 30 ./build/stdlib-tests/deep_error_task5.default < /dev/null

./build/c+ build --sanitize=undefined tests/stdlib/deep_error_task5.cp \
  -o build/stdlib-tests/deep_error_task5.ubsan
timeout 30 ./build/stdlib-tests/deep_error_task5.ubsan < /dev/null
```

All three modes: build rc 0, run rc 0, stdout as above, stderr empty.
No sanitizer report and no C+ runtime fatal in any mode.
