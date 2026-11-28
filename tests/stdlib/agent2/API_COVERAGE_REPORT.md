# API-coverage and test-quality report — final assignment (Agent 2)

**Snapshot:** 2026-11-23, branch `agent/modern-stdlib`.
**Scope:** `stdlib/*.hp` public API surface versus `tests/stdlib/deep_*.cp` deep-test corpus.
**Provenance:** `docs/stdlib/REGRESSION_INVENTORY.md`, `docs/stdlib/COVERAGE_TOOL_LIMITATIONS.md`, `docs/stdlib/TASK6_FINAL_REPORT.md`, `tests/stdlib/coverage.py`, `tests/stdlib/coverage_exec.sh`, `tests/stdlib/run_deep_tests.sh`, the `docs/stdlib/DEEP_*_CONTRACT.md` family contracts, and the `tests/stdlib/deep_*.expected` / `*.stderr.expected` / `*.stdin` fixtures.

> **Reading rule for this whole report.** Every number below is written as `count / denominator` with the method attached. This report **does not compute, round, or invent any percentage** from these counts. "Exercised" is a **static textual call-site** classification, not execution, not branch coverage, and not proof of correctness or safety. See §7 and §9.

---

## 1. Public API inventory totals (with method and denominator)

### 1.1 Headline counts

| Metric | Count | Denominator | Method |
|---|---:|---|---|
| `stdlib/*.hp` headers inspected | 21 | all files matched by `sorted(Path('stdlib').glob('*.hp'))` | header modulus from `REGRESSION_INVENTORY.md` §2 |
| Public callable entries | 381 | 381 entries returned by `coverage.py::entries()` over those 21 headers | SIG / CTOR / CLASS regexes, `SKIP` keywords, `main`, `stdlib_*` excluded (`coverage.py:24-54`) |
| Distinct bare names | 232 | same 381 entries collapsed by bare name | `REGRESSION_INVENTORY.md` §6-D1 (independent re-check: 232) |
| Entries exercised (static) | 255 | **255 / 381** | literal call regex `\bname\s*\(` (234) + compiler-inserted `drop()` credited to owning type (21) + `*_drop` owner credit (0) |
| Entries not exercised (static) | 126 | **126 / 381** | 381 − 255; per-header exact list in §8 |
| Distinct names exercised | 135 | **135 / 232** | same lexical match, name-collapsed view |
| Distinct names not exercised | 97 | **97 / 232** | 232 − 135 |
| Deep tests in corpus | 28 | 28 `tests/stdlib/deep_*.cp` files | `REGRESSION_INVENTORY.md` §6-D8 |
| Sibling stdin fixtures in corpus | 5 | 5 `deep_*.stdin` with an existing sibling `.cp` | same |

The 381 entry counts were independently reproduced by importing `coverage.py::entries()` and applying its exact hit rules to the 28 `.cp` + 5 `.stdin` corpus: `headers=21, entries=381, exercised=255, not=126, distinct=232 (135/97)`. This is the same method as the inventory, not a new measurement.

### 1.2 Per-header inventory

| Header | Entries | Exercised | Not exercised |
|---|---:|---:|---:|
| `async.hp` | 14 | 5 | 9 |
| `audio.hp` | 29 | 8 | 21 |
| `collections.hp` | 63 | 63 | 0 |
| `error.hp` | 9 | 9 | 0 |
| `file.hp` | 13 | 13 | 0 |
| `fs.hp` | 8 | 8 | 0 |
| `graphics.hp` | 16 | 7 | 9 |
| `gui.hp` | 18 | 2 | 16 |
| `http.hp` | 19 | 5 | 14 |
| `image.hp` | 10 | 1 | 9 |
| `input.hp` | 10 | 0 | 10 |
| `io.hp` | 6 | 5 | 1 |
| `json.hp` | 30 | 29 | 1 |
| `log.hp` | 14 | 12 | 2 |
| `net.hp` | 15 | 14 | 1 |
| `string.hp` | 24 | 24 | 0 |
| `thread.hp` | 21 | 17 | 4 |
| `time.hp` | 5 | 5 | 0 |
| `vector.hp` | 21 | 20 | 1 |
| `video.hp` | 25 | 3 | 22 |
| `window.hp` | 11 | 5 | 6 |
| **Total** | **381** | **255** | **126** |

### 1.3 Where the two denominators differ

- **Entry view (primary):** 381 entries. 149 of these are beyond the 232 distinct names — 80 are extra overload entries within one header (`size`, `valid`, `clear`, `drop`, `grow`, `write`, `play`, `stop`, …) and 69 are the same bare name repeated across headers (`string.size`, `json_view.size`, `vector.size`, …). (`REGRESSION_INVENTORY.md` §6-D2.)
- **Name view:** 232 distinct bare names. Matching is unqualified, so one `size(` call marks every `size` entry exercised — an intentional upward bias inherited verbatim from `coverage.py`.
- Constructors are represented by their `Class.Class` entry; there is no separate class entry (§6-D3). `stdlib_*`, `main`, and control-flow keywords are excluded exactly as in `coverage.py` (§6-D4).

---

## 2. APIs referenced by tests vs APIs executed where measurable

### 2.1 What "referenced by tests" means here

`255 / 381` entries have at least one lexically matching token in the corpus: `234 / 255` by a literal `name(` call site, `21 / 255` by a compiler-inserted `drop()` credited to an owning-class token, `0 / 255` by the `*_drop` owner rule (no header entry ends in `_drop`). The strict code-only variant — comments and string literals stripped — is **250 / 381** exercised and **131 / 381** not, because five credits rest on comment/string tokens only (`loop.loop`, `renderer.line`, and the `drop` credits for `loop`, `surface`, `window`). (`REGRESSION_INVENTORY.md` §4, §6-D5/D7.)

### 2.2 What "executed" is actually measurable on this host

The dedicated runner recorded **28 deep tests × 3 modes = 84 invocations, 0 failed, 0 skipped** (`docs/stdlib/TASK6_FINAL_REPORT.md` §2, §5). Modes are `default`, `strict-none` (`--strict --sanitize=none`), and `ubsan` (`--strict --sanitize=undefined`); every build and run is bounded (`timeout -k 5 60` build, `timeout -k 5 30` run) and a timeout is always SKIPPED, never a pass (`tests/stdlib/run_deep_tests.sh`, `DEEP_HARNESS_NOTES.md` §3). Output is checked **byte-exactly** against `28` `.expected` (stdout) and `28` `.stderr.expected` fixtures, with `5` `.stdin` fixtures supplied where required; stdout/stderr mismatches are classified `OUT-FAIL`.

**Critical limitation — function-level runtime attribution is NOT available for `.hp` headers on this host.** `tests/stdlib/coverage_exec.sh` lowers each test into one `.i` translation unit and counts executed lines of the lowered unit; when `#line` mapping back to individual `.hp` files is lost, it explicitly prints `per-header attribution unavailable on this host` (`coverage_exec.sh:75-78`; `COVERAGE_TOOL_LIMITATIONS.md` §2). It produces **no per-function, per-API execution counts**. Therefore:

- execution in this report is measured at **test granularity** (84 invocations, byte-exact fixtures), **not per API**;
- the `255 / 381` figure is static name presence, and must **never** be described as "255 APIs executed";
- no API-level "executed" count or derived percentage is claimed anywhere in this report.

### 2.3 Why the execution-coverage tool is not a correctness gate

`coverage_exec.sh` swallows non-zero exits with `|| true`, has no timeout, has no coverage threshold, and its own exit status changes only on a **build** failure (`COVERAGE_TOOL_LIMITATIONS.md` §2). Its run set is fixed at four tests (`foundation`, `io_edge_cases`, `log_test`, `collections_test`). It is usable only for the phrase "line execution of lowered headers for tests that completed", where "completed" includes a non-zero exit. It cannot establish assertions, branch outcomes, failure handling, or safety.

---

## 3. Boundary-condition coverage matrix per family (with cited test tags)

Test tags are the literal `<tag> <1|0>` / `SCENARIO <name> <cls>` labels pinned in each contract's exact stdout block. A `1` is the test's own self-check passing; it is not independent verification of the library.

| Family | Headers / tests | Boundary axes exercised | Representative cited test tags (contract) |
|---|---|---|---|
| **I/O** | `io.hp`: `deep_io_1byte`, `deep_io_token`, `deep_io_task2` | 1-byte buffer (progress/empty line), `size > INT_MAX`, exact fit vs overlong + stream alignment, CRLF at width boundary, numeric overflow/underflow/malformed/EOF, direct `stdlib_io_token` truncation return | `s1_nonempty`, `s1_empty`, `s1_null`, `s1_zero`, `s2_reject`, `s2_aligned`, `s4_exact`, `s4_crlf`, `s4_eof`, `s3_overlong`, `s3_direct`, `s5_i_over`, `s5_l_under`, `s5_f_sub`, `s5_d_inf`, `s5_eof` (`DEEP_IO_CONTRACT.md` §6); `c1_exact_eof_ok`, `c3_cr_boundary`, `c4_i_overflow`, `c6_*_eof` (`DEEP_IO_TASK2_CONTRACT.md` §1/§4) |
| **file** | `file.hp`: `deep_file_lines`, `deep_file_task2`, `deep_file_lifecycle`; cross-check in `deep_cross_family` CF4 | exact-fit at EOF, newline/CRLF boundary, overlong drain, 1-byte buffer, oversized length (`size > INT_MAX`), null handle/buffer/zero length, seek past EOF, read-only flush, open failure | `exact-fit`, `crlf`, `overlong`, `eof`, `one-byte-progress`, `null-handle`, `oversized-length` (`DEEP_FILE_CONTRACT.md` §7); `empty-open`, `exact-eof-status`, `exact-nl-tell`, `reject-status`, `seek-past-eof-eof`, `seek-invalid-whence`, `flush-readonly-ok`, `badpath-recoverable` (`DEEP_FILE_TASK2_CONTRACT.md` exact stdout); `cf4_file_full_buffer_eof_aligned`, `cf4_cross_family_aligned` (`DEEP_CROSS_FAMILY_CONTRACT.md` §3) |
| **collections** | `collections.hp` × 7 containers: `deep_collections_growth`, `deep_collections_collisions`, `deep_collections_boundary`, `deep_collections_alloc` | empty containers, min capacity clamp to 8, `SIZE_MAX`/`STDLIB_ALLOC_LIMIT` overflow refusal with zero allocation, growth at 6th insert, ring wrap, repeated-key overwrites, tombstone reuse, missing keys, NULL out-params, fixed-seed 18,432-operation stress | `bd_empty_*`, `bd_mincap_*`, `bd_maxcap_*`, `bd_bound_*` (incl. `bd_bound_deque_wrap`, `bd_bound_queue_wrap`), `bd_repeat_*`, `bd_tomb_*`, `bd_stress_*`, `bd_no_fatal` (`DEEP_COLLECTIONS_BOUNDARY_CONTRACT.md` §1/§5); `RESULT 32 0` growth, `RESULT 23 0` collisions (`DEEP_COLLECTIONS_CONTRACT.md` §5) |
| **string / vector** | `string.hp`: `deep_string_append`, `deep_string_view`, `deep_string_task4`; `vector.hp` + `error.hp`: `deep_error_vector`, `deep_vector_task4` | NULL constructors/args, append algebra + self-alias, reserve exact/limit overflow, interior-pointer alias, UTF-8 valid/invalid windows, empty slices, stale slices/views (refusal without dereference), insert/erase/copy_from/shrink boundaries, growth 0→4→8→16 | `append-success`, `append-recoverable`, `view-success`, `view-recoverable` (`DEEP_STRING_CONTRACT.md` §2); `t4_*` 86 checks (`DEEP_STRING_TASK4_CONTRACT.md` §2/§5); `E1_truncation`, `V1_growth`, `V2_empty_slices`, `V3_stale_slices`, `V6_overflow_shapes` (`DEEP_ERROR_VECTOR_CONTRACT.md` §2); `T4_INSERT_BOUNDARIES`, `T4_ERASE_BOUNDARIES`, `T4_RESERVE_EXACT_AND_OVERFLOW`, `T4_STALE_SLICE_REFUSAL`, `T4_STALE_VIEW_DROP` (`DEEP_VECTOR_TASK4_CONTRACT.md` §2) |
| **thread** | `thread.hp`: `deep_thread_guard`, `deep_thread_nested`, `deep_thread_task5` | unlock-then-drop, repeated unlock/drop, zero-init and NULL guard, nested guards (different/same mutex), cross-function release, mutex start/ join, move, re-lock after scope, cyclic lifecycle | `after_unlock`, `repeat_unlock_drop`, `holds_after_drop`, `zero_holds`, `null_holds`, `explicit_drop`, `nested_inner_holds`, `cross_relock`, `trap_rc 107` (`DEEP_THREAD_CONTRACT.md` §3); `t5_worker_rejoin_fails`, `t5_move_src_inert`, `t5_cycles_telemetry_clean`, `t5_zero_guard_released` (`DEEP_THREAD_TASK5_CONTRACT.md` §2) |
| **fs / time / net** | `fs.hp`, `time.hp`, `net.hp`: `deep_fs_time`, `deep_fs_task5`, `deep_net`, `deep_net_task5` | invalid/NUL/long paths, permissions, missing-file ops, atomic overwrite, repeated remove, 1 ns / UINT64_MAX / negative wrap / sleep boundaries; ephemeral/highest/invalid ports, bind-in-use, borrowed fd 0/1/2 vs ≥3, EOF, partial write, injected syscall faults | `deep_fs_time: 54 checks, 0 failures` (`DEEP_FS_TIME_CONTRACT.md` §1); `FS5_NUL_SIZE`, `FS5_PERM_CREATE_DENIED`, `FS5_TIME_UINT64_MAX`, `FS5_REPEATED_REMOVE`, `FS_TASK5 35 0` (`DEEP_FS_TASK5_CONTRACT.md` exact stdout); `port_ephemeral_zero`, `port_65535`, `port_invalid_range`, `borrow_no_close_012`, `eof_exact`, `partial_write_short`, `SUMMARY 8 6 0 0` (`DEEP_NET_CONTRACT.md` §5); `net_invalid_address`, `net_partial_read`, `NET_TASK5` (`DEEP_NET_TASK5_CONTRACT.md` §4) |
| **json / error / log** | `json.hp`, `error.hp`, `log.hp`: `deep_json_types`, `deep_json_stress`, `deep_json_task5`, `deep_error_vector`, `deep_error_task5`, `deep_log_task5` | typed getter ranges and wrong types, NULL args, missing members/out-of-range indices, invalid/dropped builder, non-finite values, repeated writes, bounded depth/size, malformed JSON, message truncation 255/256/257, category mapping, log level parse/name/threshold/message-length boundaries | `RESULT 65 0`, `RESULT 20 0`, bounded depth 96 / line cap 8192 / 900-element array / 64-level object (`DEEP_JSON_CONTRACT.md` §1/§3); `json_truncated_object`, `json_double_i64max`, `json_dropped_doc_views`, `JSON_TASK5` (`DEEP_JSON_TASK5_CONTRACT.md` exact stdout); `E1_truncation`, `E3_null_handle`, `E4_categories` (`DEEP_ERROR_VECTOR_CONTRACT.md` §2); `err_bound_255`, `err_bound_256`, `err_bound_257`, `ERROR_TASK5` (`DEEP_ERROR_TASK5_CONTRACT.md` exact stdout); `t5_parse_numeric_5`, `t5_len_255`, `t5_len_4095_bounds`, `LOG_TASK5 49 0` (`DEEP_LOG_TASK5_CONTRACT.md` §2/§3) |

**Coverage qualifier:** a boundary tag passing means the self-check matched the contract's expected value in that run. It does not prove all branches of the implementation were reached, nor that untested overloads of the same bare name behaved identically (§9-M2).

---

## 4. Failure-condition coverage (allocation injection, invalid input, EOF, overflow)

### 4.1 Allocation-failure injection counts

Failure injection is a deterministic logical counter (`stdlib_alloc_fail_at/after/reset`), never physical memory exhaustion (`TASK6_FINAL_REPORT.md` §7).

| Suite | Result | Denominator / method |
|---|---|---|
| `deep_collections_alloc.cp` | `ALLOC 59 0` | 13 allocation points enumerated with `stdlib_alloc_seq()`: `intset` insert/grow = **2** each; `deque::push_back`, `push_front`, wrapped growth, `list` push_back/front (empty and non-empty), `stack::push` first/growth, `queue::push` first/wrapped growth = **1** each. For every `N = 1..count`: return `-1`, state/order/capacity unchanged, telemetry `alloc_count == free_count`, `alloc_failures >= 1`, reset retry succeeds (`DEEP_COLLECTIONS_ALLOC_CONTRACT.md` §1/§2/§5) |
| `deep_collections_growth.cp` | `RESULT 32 0` | intmap/strmap rebuild 1/2/3 + key copy at 4, rollback + retry (`DEEP_COLLECTIONS_CONTRACT.md` §1; `TASK6_FINAL_REPORT.md` §7) |
| `deep_string_task4.cp` | `STRING_TASK4 86 0` | constructor allocation failure → `valid()==0`; `copy_from` single-point atomicity; `fail_at(2)` succeeds because `append` performs exactly one allocation; alloc/free balance (`DEEP_STRING_TASK4_CONTRACT.md`; `TASK6_FINAL_REPORT.md` §7) |
| `deep_vector_task4.cp` | `VECTOR_TASK4 16 0` | `insert`, `copy_from`, `shrink_to_fit` failure atomicity with generation preserved and retry (`DEEP_VECTOR_TASK4_CONTRACT.md` §2) |
| `deep_cross_family.cp` CF2 | 15 checks, 0 failures | `stdlib_alloc_fail_after(0)` on full vector `push_back`, then `reserve(64)`; exactly one injected failure, stale slice refusal, `free_count == alloc_count` |
| `tests/libc/test_alloc_fault_matrix.c` | `294 / 294` checks ok | `cplus_alloc_fail_at/after/reset`, plain + UBSan (`TASK6_FINAL_REPORT.md` §7; reported from that document, not re-run here) |
| `deep_io_token.cp` `s5_i_alloc_fail` | 1 check | allocation-free reader: read_int still succeeds under `fail_after(0)` (`DEEP_IO_CONTRACT.md` §7) |

**Documented injection gaps (not defects):** `json.hp` builder/document and `image.hp` use external allocators, so `stdlib_alloc_fail_at/after` has no effect on them; `file.hp` performs no `stdlib_alloc`; deterministic failure coverage for JSON comes instead from builder argument/value-level refusals (`DEEP_JSON_CONTRACT.md` §3; `DEEP_FILE_CONTRACT.md` decision 9).

### 4.2 Invalid input

- **NULL arguments:** `s1_null`, `s1_zero` (I/O); `bd_invalid_*`, `bd_nullout_*` (collections); `T4_NULL_INVALID_ARGS` (vector); `E3_null_handle` (error); `member(NULL)`, null getter `out` pointers, `add_string(NULL, ...)`, `json_parse_file(NULL)` (JSON); `t5_open_null_*`, `t5_open_bad_*`, `json_builder_null_err_args` (log/JSON); `net_invalid_address`, `invalid_handles` (net); `FS5_NUL_SIZE`, `FS5_NUL_REMOVE`, `FS5_NUL_MKDTEMP_INVALID` (fs).
- **Invalid ports/paths/modes:** `port_invalid_range` (`-1`, `70000`, `INT_MIN`), `bind_in_use_fork`, `fault_socket_emfile` (net); `badpath-open`, `badmode-open` (file task2); `FS5_EMPTY_OPS`, `FS5_LONG_PATH`, `FS5_PERM_CREATE_DENIED` (fs task5).
- **Compile-time vs runtime boundary:** copying an owning `file` / `json` is rejected at compile time, not executed at runtime (`DEEP_FILE_CONTRACT.md` "Fatal"; `DEEP_JSON_CONTRACT.md` §2.4).

### 4.3 EOF

`STDLIB_EOF` is treated as success-with-clear-error, not an error (`DEEP_IO_CONTRACT.md` §0). Cited tags: `s4_eof`, `s1_eof` (`deep_io_1byte`), `s5_eof` (`deep_io_token`), `c6_i_eof`, `c6_l_eof`, `c6_f_eof`, `c6_d_eof` (`deep_io_task2`), `empty-read-eof`, `exact-eof-next-eof`, `empty-readline-again-eof`, `seek-past-eof-eof` (`deep_file_task2`), `eof`, `after-overlong-eof` (`deep_file_lines`), `eof_exact` (`deep_net`), `FS5_TIME_*` guard cases (`deep_fs_task5`).

### 4.4 Overflow / range rejection

`read_int` `2147483648`/`-2147483649`; `read_long` `9223372036854775808`/`-9223372036854775809`; `read_float` `1e39`/underflow; `read_double` `1e400`/`inf`/subnormals (`s5_*` tags). Container: `bd_maxcap_intmap/intset/strmap`, `bd_maxcap_deque_reserve`, `V6_overflow_shapes`, `T4_RESERVE_EXACT_AND_OVERFLOW`, `T4_PUSH_GROWTH_OVERFLOW`, string `LIMIT+1` and `count+extra+1 > LIMIT` (`DEEP_STRING_CONTRACT.md` reserve boundaries). JSON: `INT_MAX+1`, `INT_MIN-1`, `1e400` → `RANGE`/`INVALID` with untouched out-param. Time: `FS5_TIME_UINT64_MAX`, `FS5_TIME_NEGATIVE_WRAP`. No request near the 1 TiB allocator limit is issued; host memory is never stressed (`DEEP_ERROR_VECTOR_CONTRACT.md` V6).

---

## 5. Resource-lifecycle coverage (double close/drop, borrow, move)

| Lifecycle axis | Cited evidence | What is pinned |
|---|---|---|
| **Double close / drop** | `deep_file_lifecycle`: `close-double-drop`, `move-double-drop`, `borrow-close`, `borrow-drop`, `fd-leak` (`DEEP_INVENTORY_RESULTS.md` §failure detail); `bd_drop_*` × 7 containers (`DEEP_COLLECTIONS_BOUNDARY_CONTRACT.md` §1 item 9); `T4_CLEAR_DROP_REUSE` (`DEEP_VECTOR_TASK4_CONTRACT.md` §2); string `drop()` second call inert (`DEEP_STRING_CONTRACT.md` "Operations after drop"); `net_repeated_drop`, `net_repeated_drop_no_leak` (`DEEP_NET_TASK5_CONTRACT.md` §4); `FS5_REPEATED_REMOVE` (`DEEP_FS_TASK5_CONTRACT.md` exact stdout) | second `drop()`/`close()` is recoverable or inert; accessors refuse after drop; a post-drop put/push re-initialises from zero state; fd count returns to baseline |
| **Borrow** | `file_borrow` (`deep_file_lifecycle`), `listener_borrow` / `connection_borrow` (`deep_net`), `borrow_no_close_012`, `borrow_ge3_no_close` (`DEEP_NET_CONTRACT.md` §5); `string_view` / `json_view` / `vector::slice` borrowed windows (`DEEP_STRING_CONTRACT.md` §"string_view lifetime", `DEEP_JSON_CONTRACT.md` §2.4, `DEEP_VECTOR_TASK4_CONTRACT.md` area 7/8) | borrowed handles never close fd 0/1/2 or ≥3 on destruction; views are snapshots and are never dereferenced after owner drop; staleness is detected via generation/pointer comparison, not dereference |
| **Move / transfer** | `deep_file_lifecycle` `move-double-drop`; CF3 `cplus_move` of `intmap` + `stdlib_transfer_ownership` with generation-observed stale alias (`DEEP_CROSS_FAMILY_CONTRACT.md` §2 CF3); `t5_move_src_holds`, `t5_move_dst_holds`, `t5_move_src_inert`, `t5_move_relock_after` (`DEEP_THREAD_TASK5_CONTRACT.md`); `err_move_state` (`DEEP_ERROR_TASK5_CONTRACT.md`) | moved-from owner is inert and never read; single ownership retained; generation of transferred block is fresh; a second transfer from NULL source is rejected |
| **Leak / balance** | `no_fd_leak` (`DEEP_NET_CONTRACT.md`), `net_task5_no_fd_leak`, `t5_cycles_fd_stable`, `t5_cleanup_no_leftovers`, `FS5_RESOURCE_NO_LEAK`, `FS5_NO_LEFTOVERS`, `V7_alloc_balance`, `T4_COPY_FROM_BALANCE`, `cleanup-no-leftovers` | telemetry `free_count == alloc_count` and fd counts unchanged; cleanup after failures verified |
| **Lock lifecycle** | `after_unlock`, `repeat_unlock_drop`, `holds_after_drop`, `explicit_drop`, `nested_*`, `seq_*`, `cross_*`, positive controls `trap_rc 107` (ERRORCHECK double-unlock) and `deadlock_detector 1` (watchdog SIGALRM) (`DEEP_THREAD_CONTRACT.md` §1-§3) | exactly one unlock per acquisition; no double unlock; re-lock after scope succeeds; fatal positive controls are confined to forked children |

**Not proven:** these are self-checks observed under the three runner modes. ASan is unavailable for most suites on this host (`TASK6_FINAL_REPORT.md` §6), so absence of a sanitizer report is **not** a memory-safety proof.

---

## 6. Cross-module coverage

### 6.1 `tests/stdlib/deep_cross_family.cp` (present, cited)

Six scenarios exercise **interactions between** headers rather than one family in isolation (`docs/stdlib/DEEP_CROSS_FAMILY_CONTRACT.md`):

| Scenario | Cross-module interaction | Checks |
|---|---|---:|
| CF1 | `string` key copied into `strmap`; source `clear()` / reallocating `reserve()+append()` / `drop()` must not change map value — pins `strmap::put`'s owned-key copy across a lifetime boundary | 13 |
| CF2 | `error::code()` values pushed into `vector`; injected OOM at full capacity; stale `slice` refusal; telemetry balance across error+vector+allocator | 15 |
| CF3 | moved `intmap` + `stdlib_calloc` + `stdlib_transfer_ownership`; generation-observed stale alias; second transfer rejected | 13 |
| CF4 | `file::read_line` vs `io::read_line` on identical content (exact-fit, overlong, CRLF, EOF) — cross-family alignment; a reproduced discrepancy was fixed in `file.hp` | 18 |
| CF5 | `string → string_view → json_builder::add_string` (yyjson copies text); source dropped before serialise; `json_view` read while document alive | 11 |
| CF6 | one `stdlib_set_error_callback` receives notes from string, vector, strmap, file, json, log, allocator, ownership and error families; categories/counters checked | 19 |

Total **89 / 89 checks, 0 failures** in all three modes; final line `CROSS_FAMILY 89 0`. A byte-exact fixture `tests/stdlib/deep_cross_family.expected` (7 lines, ending `CROSS_FAMILY 89 0`) is now present and is compared by the runner. All owning locals use unique names because C+ RAII lowering tracks owners by name per function (CF1/CF3 and `DEEP_THREAD_CONTRACT.md` F3).

### 6.2 `tests/stdlib/agent2/cross_module.cp` (present; separate, unverified harness path)

A sibling agent created `tests/stdlib/agent2/cross_module.cp` (with `cross_module.stdin`) while this report was being written. Its own header comment describes a single deterministic program covering ten assigned areas: file→string read, JSON parse, intmap/vector storage and growth, drop of containers before strings, file-error propagation through `error`, `fs_exists` gating, injected allocation failure + recovery, repeated failed/valid cycles with telemetry balance, and idempotent-drop / no-double-release checks. It prints `<tag> <0|1>` per check and a final `CROSS_MODULE <checks> <failures>` line, exiting 0 only on zero failures. This is a **designed contract**, not an observed pass.

**Result status at snapshot:** the sibling harness's `tests/stdlib/agent2/results/summary.txt` records `cross_module ... BUILD_FAILURE ... compiler exit 1` (`SUMMARY PASS=0 FAIL=0 BUILD_FAILURE=1 TIMEOUT=0 SKIPPED=0 BLOCKED=0 TOTAL=1`). The build log shows a C+ lowering error at `cross_module.cp:192` (`cplus_move` applied to an rvalue `json`, then `cm_doc` undeclared). The sibling agent was still iterating on the file; **no 3-mode pass, no check count, and no byte-exact fixture result is claimed here**. Any later green run must be cited from that agent's own updated results, with its own denominator.

**Measurement ambiguity to flag:** `tests/stdlib/run_deep_tests.sh` selects sources with the glob `tests/stdlib/deep_*.cp` (`run_deep_tests.sh:189`). `agent2/cross_module.cp` is neither named `deep_*` nor located in `tests/stdlib/`, so it is **not** auto-discovered by that runner, does **not** appear in the 84-invocation tally, and is **not** part of the 28-file / 5-stdin static scan corpus behind §1. It must be reported on its own harness path (`agent2/run_all.sh` / `results/`), not folded into the 381-entry inventory or the 255/126 exercised split.

---

## 7. Name presence is not proof of execution or safety — explicit statement

1. **A matching name is not an executed call.** `coverage.py` is a text search: a call site in a comment, string literal, dead branch, `#if 0` region, or a non-run example still counts; an indirect call through a pointer, operator, macro, or callback with no literal `name(` is invisible (`COVERAGE_TOOL_LIMITATIONS.md` §1, false-positive/false-negative classes).
2. **A matching name is not a checked effect.** `foo();` counts exactly as much as `assert(foo() == 0);`. Return values, assertions, postconditions, branch outcomes, failure paths, and which overload was selected are not examined.
3. **A passing deep test is not a per-API result.** The runner records 84 test invocations with byte-exact fixtures; it does not record which of the 381 API entries executed inside them, because function-level `.hp` attribution is unavailable on this host (§2.2).
4. **No safety claim follows.** Sanitizer/UBSan evidence and byte-exact fixtures are behavioural evidence for the scenarios they cover; they do not prove the library is correct, hardened, regression-free, or memory-safe (ASan unavailable). This document is a static call-site inventory plus a test-granularity result record — not an execution-coverage proof (`REGRESSION_INVENTORY.md` §7; `TASK6_FINAL_REPORT.md` §3).
5. **No percentage is invented.** All figures here are counts with explicit denominators (`255 / 381`, `126 / 381`, `135 / 232`, `84` invocations, etc.). The `80.0` threshold in `coverage.py:22` is quoted as the tool's own gate, not applied to or computed from these numbers. Where other documents print percentages (e.g. a library-wide "100.0% API coverage"), `COVERAGE_TOOL_LIMITATIONS.md` §4 records that as an overstatement of `coverage.py` and it is **not** adopted here.

---

## 8. Exact list of APIs without direct tests by module (from the inventory)

**Total: 126 of 381 entries not exercised by any deep test** (`REGRESSION_INVENTORY.md` §5). Listed verbatim per module; "0" means no unexercised entries **by this static measure**, which does not imply every branch or failure path ran.

| Module | Not exercised | Count |
|---|---|---:|
| `async.hp` | loop.run, loop.stop, loop.now_ms, timer.timer, timer.stop, timer.is_active, timer.drop, sleep_ms, uv_error_text | 9 |
| `audio.hp` | audio_context.audio_context, audio_context.drop, audio_sound.audio_sound, audio_sound.play, audio_sound.stop, audio_sound.playing, audio_sound.drop, audio_engine.audio_engine, audio_engine.set_volume, audio_engine.play, audio_engine.load, audio_engine.drop, audio_device.stop, audio_device.running, audio_device.drop, audio_decoder.audio_decoder, audio_decoder.channels, audio_decoder.sample_rate, audio_decoder.frame_count, audio_decoder.drop, audio_sleep_ms | 21 |
| `collections.hp` | (none) | 0 |
| `error.hp` | (none) | 0 |
| `file.hp` | (none) | 0 |
| `fs.hp` | (none) | 0 |
| `graphics.hp` | surface.surface, surface.width, surface.height, surface.pixel, renderer.renderer, renderer.fill_rect, renderer.present, renderer.set_vsync, renderer.drop | 9 |
| `gui.hp` | gui.gui, gui.handle_event, gui.input_begin, gui.input_motion, gui.input_button, gui.input_end, gui.window_end, gui.layout_row_dynamic, gui.layout_row_static, gui.button, gui.label, gui.slider, gui.widget_bounds, gui.render, gui.style_text_color, gui.drop | 16 |
| `http.hp` | http.http, http.drop, http_version, http_headers.http_headers, http_headers.add, http_headers.drop, http_client.http_client, http_client.set_timeout, http_client.follow_redirects, http_client.set_headers, http_client.header_lines, http_client.status, http_client.bytes, http_client.drop | 14 |
| `image.hp` | image.image, image.width, image.height, image.channels, image.size_bytes, image.pixel, image.write_png, image.write_bmp, image.drop | 9 |
| `input.hp` | poll_event, event_is_quit, event_is_key_down, event_is_key_up, event_scancode, event_text, keyboard_state, key_pressed, mouse_state, mouse_button_left | 10 |
| `io.hp` | println | 1 |
| `json.hp` | json_view.json_view | 1 |
| `log.hp` | log_stderr, log_stdout | 2 |
| `net.hp` | connection.connection | 1 |
| `string.hp` | (none) | 0 |
| `thread.hp` | condvar.condvar, condvar.wait, condvar.broadcast, condvar.drop | 4 |
| `time.hp` | (none) | 0 |
| `vector.hp` | slice.slice | 1 |
| `video.hp` | video_error_text, frame.frame, frame.width, frame.height, frame.format, frame.pts, frame.drop, video_codec_id, decoder.decoder, decoder.width, decoder.height, decoder.next_frame, decoder.drop, media.media, media.stream_count, media.video_index, media.video_width, media.video_height, media.video_codec, media.duration_ms, media.open_decoder, media.drop | 22 |
| `window.hp` | sdl.sdl, sdl.subsystem_flags, sdl.drop, sdl_backend_error, window.window, window.set_title | 6 |
| **Total** | — | **126** |

Largest unexercised modules by count: `video.hp` (22), `audio.hp` (21), `gui.hp` (16), `http.hp` (14), `input.hp` (10), `async.hp` (9), `graphics.hp` (9), `image.hp` (9). These are largely GUI/media/event/input families whose external development files (SDL3, libcurl, libuv, ffmpeg) are unavailable on this host; that is a coverage limitation, not a pass (`TASK6_FINAL_REPORT.md` §9; `tests/stdlib/agent2/existing_tests.txt` documents the exclusion of such tests from the non-deep curated run set). The `net::connection` constructor and `vector::slice` constructor are spelling-only gaps: their factories `connection_borrow` / `vector.view()` are exercised (`REGRESSION_INVENTORY.md` §4 list for `net.hp` and `vector.hp`).

---

## 9. Measurement ambiguities (consolidated)

| # | Ambiguity | Impact on counts |
|---|---|---|
| M1 | Entry vs distinct-name denominator | 381 entries vs 232 names; 255/126 vs 135/97. Both from the same parser; entry view is primary because overloads are distinct C++ signatures and it matches `coverage.py`'s denominator. |
| M2 | Unqualified lexical matching | 149 entries beyond 232 distinct names (80 within-header overloads, 69 cross-header repeats). One `size(` marks every `size` entry exercised — upward bias. |
| M3 | Compiler-inserted `drop()` | 21 credits with no literal `drop(` call; 3 owner tokens (`loop`, `surface`, `window`) occur only in prose comments. |
| M4 | Raw text includes comments/strings | Two literal credits (`loop.loop`, `renderer.line`) plus the 3 drop tokens in M3 are comment/string-only. Strict code-only variant is **250 / 381** exercised, **131 / 381** not. Headline 255/126 is the faithful `coverage.py` result. |
| M5 | Corpus snapshot | Counts scanned exactly 28 `deep_*.cp` + 5 sibling `.stdin`. Sibling agents were adding tests concurrently; any added `deep_*` test requires recomputation. `agent2/cross_module.cp` has appeared but sits outside the corpus and is not auto-selected by `run_deep_tests.sh`; its snapshot status is `BUILD_FAILURE` (§6.2). |
| M6 | No function-level runtime attribution for `.hp` on this host | `coverage_exec.sh` prints "per-header attribution unavailable on this host" and emits lowered-unit line counts only; no per-API execution counts exist. Execution is reported at test granularity only. |
| M7 | `coverage_exec.sh` is not a correctness gate | `|| true` swallows non-zero exits, no timeout, no threshold, exit status changes only on build failure, fixed 4-test run set. |
| M8 | Fixture presence | 28 stdout + 28 stderr byte-exact fixtures and 5 stdin fixtures; `deep_cross_family` originally documented no fixture but a 7-line `.expected` is now compared. Contract docs' "no expected needed" statement is superseded by the file's presence. |
| M9 | "Exercised" vs "executed" vs "safe" | Three different claims; this report establishes only the first statically and the runner's invocation counts behaviourally. No safety or correctness proof is made (§7). |
| M10 | External-allocator / unavailable-backend gaps | `json.hp` and `image.hp` bypass the checked allocator; ASan is unavailable for most suites; GUI/media/input families are excluded from the curated non-deep run set. Absence of a report here is not proof of absence of a defect. |

---

### Bottom line

- **Inventory:** 21 headers, **381 / 381** public callable entries enumerated; **255 / 381** statically exercised, **126 / 381** not; **232** distinct names (**135 / 232** exercised).
- **Test quality:** **28 deep tests × 3 modes = 84 / 84** invocations passed with byte-exact stdout/stderr fixtures; failure, boundary, lifecycle, and cross-family behaviour is covered by the cited self-check tags.
- **Separate agent2 test:** `agent2/cross_module.cp` is a distinct, non-`deep_*` harness path; at snapshot it recorded `BUILD_FAILURE` (compiler exit 1) and contributes nothing to the 84 invocations or the inventory counts (§6.2).
- **Limits:** function-level runtime attribution for `.hp` headers is unavailable on this host, so execution is measured at test granularity, not per API; name presence is not proof of execution or safety; no percentage has been invented and every count carries its denominator.
