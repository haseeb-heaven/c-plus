# Agent 2 audit — stdlib test inventory (static)

Scope: `tests/stdlib/`, the stdlib-relevant subset of `tests/libc/test_*.c`, and the
nine named runner scripts. Everything below was enumerated from the filesystem and
from runner **script logic** (glob patterns, hardcoded lists, case/guard
statements), not from execution results. Execution results are in `baseline.md`.

Snapshot: HEAD `11831a2740d167b5c14586e490e6348c271f4481`, 2026-11-22T23:28:12Z.

## Headline counts (computed, not estimated)

| Artifact | Count |
|---|---:|
| `tests/stdlib/*.cp` excluding `deep_*.cp` | 24 |
| `tests/stdlib/deep_*.cp` | 28 |
| `tests/stdlib/compile_time/*.cp` | 52 |
| `tests/stdlib/*.stdin` | 11 |
| `tests/stdlib/*.expected` (excluding `.stderr.expected`) | 42 |
| `tests/stdlib/*.stderr.expected` | 28 |
| `tests/stdlib/compile_time/*.json` manifests | 8 |
| `tests/libc/test_*.c` (all, context) | 97 |
| `tests/libc/test_*.c` selected as stdlib-relevant here | 7 |

Derived facts: deep runner invocations = 28 files × 3 modes = **84**; deep stdin
fixtures = 5; deep tests with non-empty `stderr.expected` = 3
(`deep_string_append`, `deep_string_view`, `deep_thread_guard`); non-deep stdin
fixtures = 6; non-deep `.expected` = 14 (13 with a matching `.cp` plus an orphan
`multi_tu.expected`); the remaining 28 `.expected` files belong to deep tests.
All 28 deep tests have both an `.expected` and a `.stderr.expected`.

## Runner semantics — verified from script logic

Runner key used in the tables below: **deep** = `tests/stdlib/run_deep_tests.sh`;
**stdlib** = `tests/stdlib/run_stdlib_tests.sh`; **ct** =
`tests/stdlib/compile_time/run_compile_time.sh`; **evidence** =
`tests/run_all_evidence.sh`; **cov-exec** = `tests/stdlib/coverage_exec.sh`;
**cov-static** = `tests/stdlib/coverage.py`; **compiler** = `tests/run_tests.sh`;
**runtime** = `tests/runtime/run_runtime_tests.sh`; **safety** =
`tests/safety/run_safety_matrix.sh`.

| Runner | What it actually selects | stdout check | stderr check | In the 9-command baseline? | In `run_all_evidence.sh`? |
|---|---|---|---|---|---|
| `tests/run_tests.sh` | `tests/cases/*.c`, `*.cp`; `tests/compile_fail/*.c`, `*.cp`; `tests/runtime_fail/*.c`, `*.cp`; `tests/legacy/plain.c` | `.expected` for cases | substring in `.stderr` for runtime_fail | yes (#1) | yes (line 21) |
| `tests/libc/run_libc_tests.sh` | glob `tests/libc/test_*.c` minus `*_attack.c` and `test_corpus_format.c` → 85 of 97 | no fixture; exit code | no | yes (#2) | yes (line 22) |
| `tests/crt/unit/run_unit_tests.sh` | CRT unit set | n/a | n/a | yes (#3) | yes (line 23) |
| `tests/crt/run_crt_tests.sh` | CRT corpus | n/a | n/a | yes (#4) | via `run_all_crt.sh` (line 24) |
| **deep** | glob `tests/stdlib/deep_*.cp` only; 3 modes (default, strict-none, ubsan); `timeout` on build and run | byte-exact `<name>.expected` | byte-exact `<name>.stderr.expected` | yes (#5) | yes (line 31) |
| **ct** | `sorted(compile_time/*.json)` → 52 `case["file"]` sources; `c+ check --strict` + `c+ build --strict --sanitize=none` | no fixture; declared `EXPECT` in JSON | tagged-diagnostic inspection | yes (#6) | yes (line 32) |
| **stdlib** | glob `tests/stdlib/*.cp` — **this includes all 28 `deep_*.cp`**; `bench_*` explicitly skipped; `video_test`/`sdl_test`/`gui_test`/`http_test`/`async_test` dependency-gated; plus `examples/*.cp`, optional `coverage`, `multi_tu`, `class_linkage` | `<name>.expected` only | **none** — `.stderr.expected` is never read | **no** | **no** |
| **cov-exec** | hardcoded 4 tests: `foundation`, `io_edge_cases`, `log_test`, `collections_test` | discarded (`>/dev/null 2>&1`) | discarded; exit status ignored (`\|\| true`) | no | no |
| **cov-static** | API-surface reachability parser; invoked by **stdlib** only when pattern `coverage` matches | no | no | no (only via stdlib pattern) | no |
| `tests/family_gaps/run_family_tests.sh` | family suites | n/a | n/a | yes (#7) | yes (line 30) |
| `tests/integration/run_integration.sh` | integration suite | n/a | n/a | yes (#8) | yes (line 36) |
| `tests/run_all_evidence.sh` | aggregates the 23 suites on lines 21–43 | per-suite | per-suite | yes (#9) | n/a |
| **runtime** | hardcoded `tests/runtime/test_runtime_recover.c`, `demo_unguarded.c`, `test_runtime_cleanup.c` | grep for ok marker | grep for UBSan diagnostic | no | **no** |
| **safety** | hardcoded `tests/safety/test_scenarios_c.c`, `test_scenarios_cp.cp`, `test_scenarios_threads.c` | grep for ok marker | no | no | **no** |

## Required explicit statements

1. **`tests/stdlib/coverage_exec.sh` contains `|| true` on the test-binary run
   (line 50: `LLVM_PROFILE_FILE=... "$OUT/$name" <"$input" >/dev/null 2>&1 || true`)
   and discards both stdout and stderr. It is an execution-coverage measurement
   tool, **not a correctness runner**: a segfault, wrong output or non-zero exit
   of the profiled binary cannot fail this script. It also has no timeout. It is
   only invoked (as "informational") by `tests/stdlib/run_all.sh` line 25, and is
   absent from the 9-command baseline and from `run_all_evidence.sh`.**
2. **`tests/run_tests.sh` does NOT execute `deep_*.cp` (nor any
   `tests/stdlib/*.cp`).** Its loops are restricted to `tests/cases/`,
   `tests/compile_fail/` and `tests/runtime_fail/` (plus `tests/legacy/plain.c`).
   Deep tests are executed only by `run_deep_tests.sh` (correctness runner) and
   incidentally by `run_stdlib_tests.sh` (stdout-only glob).
3. `run_all_evidence.sh` does **not** call `run_stdlib_tests.sh`, `runtime`,
   `safety`, `cov-exec` or `cov-static`. `run_stdlib_tests.sh` and `cov-exec` are
   reachable only via `tests/stdlib/run_all.sh`.

## A. `tests/stdlib/*.cp` (non-deep) — 24 files

"Executed by" is from script logic: **stdlib** = matched by the `tests/stdlib/*.cp`
glob; `bench_*` are explicitly skipped; the five dependency-gated names skip when
their system library/fixture is missing. None of these 24 files is executed by
any of the 9 baseline commands (the broad **stdlib** runner is not among them and
is not called by `run_all_evidence.sh`).

| File | stdin fixture | expectation fixture(s) | Executed by | Classification |
|---|---|---|---|---|
| `adversarial.cp` | — | `adversarial.expected` | stdlib (glob) | stdlib runtime / adversarial |
| `async_test.cp` | — | — (exit code only) | stdlib (glob; skip if libuv missing) | stdlib module smoke — async |
| `audio_test.cp` | — | — (exit code only) | stdlib (glob) | stdlib module smoke — audio |
| `bench_compare.cp` | — | — | **skipped** by stdlib (`bench_*`) | benchmark, not a test |
| `bench_extra.cp` | — | — | **skipped** by stdlib (`bench_*`) | benchmark, not a test |
| `bench_stdlib.cp` | — | — | **skipped** by stdlib (`bench_*`) | benchmark, not a test |
| `collections_test.cp` | — | `collections_test.expected` | stdlib (glob); cov-exec (line 93) | stdlib runtime — collections |
| `containers_safety_test.cp` | — | `containers_safety_test.expected` | stdlib (glob) | stdlib runtime — container safety |
| `core_extras_test.cp` | — | `core_extras_test.expected` | stdlib (glob) | stdlib runtime — core extras |
| `eof.cp` | `eof.stdin` | `eof.expected` | stdlib (glob) | stdlib runtime — I/O / EOF |
| `foundation.cp` | `foundation.stdin` | `foundation.expected` | stdlib (glob); cov-exec (line 93) | stdlib runtime — foundation |
| `gui_test.cp` | — | — (exit code only) | stdlib (glob; skip if SDL3 missing) | stdlib module smoke — GUI |
| `http_test.cp` | — | — (exit code only) | stdlib (glob; skip if libcurl missing) | stdlib module smoke — HTTP |
| `image_test.cp` | — | — (exit code only) | stdlib (glob) | stdlib module smoke — image |
| `interactive_reprompt.cp` | `interactive_reprompt.stdin` | `interactive_reprompt.expected` | stdlib (glob) | stdlib runtime — interactive |
| `io_edge_cases.cp` | `io_edge_cases.stdin` | `io_edge_cases.expected` | stdlib (glob); cov-exec (line 93) | stdlib runtime — I/O edge cases |
| `json_test.cp` | — | `json_test.expected` | stdlib (glob) | stdlib runtime — JSON |
| `log_test.cp` | — | `log_test.expected` | stdlib (glob); cov-exec (line 93) | stdlib runtime — logging |
| `negative.cp` | — | `negative.expected` | stdlib (glob) | stdlib runtime — negative cases |
| `net_test.cp` | — | — (exit code only) | stdlib (glob) | stdlib module smoke — net |
| `safety_regressions.cp` | — | `safety_regressions.expected` | stdlib (glob) | stdlib runtime — safety regressions |
| `sdl_test.cp` | — | — (exit code only) | stdlib (glob; skip if SDL3 missing) | stdlib module smoke — SDL |
| `stdlib_hardening_test.cp` | `stdlib_hardening_test.stdin` | `stdlib_hardening_test.expected` | stdlib (glob) | stdlib runtime — hardening |
| `video_test.cp` | — | — (exit code only) | stdlib (glob; skip without ffmpeg fixture) | stdlib module smoke — video |

Orphan fixtures with no matching `.cp`: `multi_tu.expected`, `multi_tu.stdin`.
`multi_tu` is a three-TU program handled by an explicit block in **stdlib**
(lines 252–262) with an inline `printf '7\none\ntwo\nthree\n'` and no fixture
comparison; those two fixture files are therefore unused by that block. It is
also not in the 9 baseline commands.

## B. `tests/stdlib/deep_*.cp` — 28 files

Every row: stdin fixture shown when present (otherwise `/dev/null`); expectation
fixtures are byte-exact `<name>.expected` **and** `<name>.stderr.expected` (the
latter is 0 bytes except where noted). Executed by **deep** in all 3 modes
(default, strict-none, ubsan), by **stdlib** (stdout `.expected` only — stderr
fixtures are not compared there), and by **evidence** line 31 via **deep**.

| File | stdin fixture | expectation fixtures | Executed by | Classification |
|---|---|---|---|---|
| `deep_collections_alloc.cp` | — (uses `stdlib_alloc_seq()`) | `.expected`, `.stderr.expected` (0 B) | deep ×3, stdlib, evidence | deep — collections / allocation failure |
| `deep_collections_boundary.cp` | — | `.expected`, `.stderr.expected` (0 B) | deep ×3, stdlib, evidence | deep — collections / boundaries |
| `deep_collections_collisions.cp` | — | `.expected`, `.stderr.expected` (0 B) | deep ×3, stdlib, evidence | deep — collections / hash collisions |
| `deep_collections_growth.cp` | — | `.expected`, `.stderr.expected` (0 B) | deep ×3, stdlib, evidence | deep — collections / rebuild growth |
| `deep_cross_family.cp` | — (yyjson linked) | `.expected`, `.stderr.expected` (0 B) | deep ×3, stdlib, evidence | deep — cross-family |
| `deep_error_task5.cp` | — | `.expected`, `.stderr.expected` (0 B) | deep ×3, stdlib, evidence | deep — error |
| `deep_error_vector.cp` | — | `.expected`, `.stderr.expected` (0 B) | deep ×3, stdlib, evidence | deep — error/vector |
| `deep_file_lifecycle.cp` | — | `.expected`, `.stderr.expected` (0 B) | deep ×3, stdlib, evidence | deep — file lifecycle |
| `deep_file_lines.cp` | `deep_file_lines.stdin` | `.expected`, `.stderr.expected` (0 B) | deep ×3, stdlib, evidence | deep — file line reading |
| `deep_file_task2.cp` | — | `.expected`, `.stderr.expected` (0 B) | deep ×3, stdlib, evidence | deep — file |
| `deep_fs_task5.cp` | — | `.expected`, `.stderr.expected` (0 B) | deep ×3, stdlib, evidence | deep — filesystem |
| `deep_fs_time.cp` | — | `.expected`, `.stderr.expected` (0 B) | deep ×3, stdlib, evidence | deep — filesystem time |
| `deep_io_1byte.cp` | `deep_io_1byte.stdin` | `.expected`, `.stderr.expected` (0 B) | deep ×3, stdlib, evidence | deep — 1-byte I/O |
| `deep_io_task2.cp` | `deep_io_task2.stdin` | `.expected`, `.stderr.expected` (0 B) | deep ×3, stdlib, evidence | deep — I/O |
| `deep_io_token.cp` | `deep_io_token.stdin` | `.expected`, `.stderr.expected` (0 B) | deep ×3, stdlib, evidence | deep — token I/O |
| `deep_json_stress.cp` | `deep_json_stress.stdin` | `.expected`, `.stderr.expected` (0 B) | deep ×3, stdlib, evidence | deep — JSON stress |
| `deep_json_task5.cp` | — | `.expected`, `.stderr.expected` (0 B) | deep ×3, stdlib, evidence | deep — JSON |
| `deep_json_types.cp` | — | `.expected`, `.stderr.expected` (0 B) | deep ×3, stdlib, evidence | deep — JSON types |
| `deep_log_task5.cp` | — | `.expected`, `.stderr.expected` (0 B) | deep ×3, stdlib, evidence | deep — logging |
| `deep_net.cp` | — | `.expected`, `.stderr.expected` (0 B) | deep ×3, stdlib, evidence | deep — networking |
| `deep_net_task5.cp` | — | `.expected`, `.stderr.expected` (0 B) | deep ×3, stdlib, evidence | deep — networking |
| `deep_string_append.cp` | — | `.expected`, **`.stderr.expected` 139 B** | deep ×3, stdlib (stdout only), evidence | deep — string append failures |
| `deep_string_task4.cp` | — | `.expected`, `.stderr.expected` (0 B) | deep ×3, stdlib, evidence | deep — string allocation atomicity |
| `deep_string_view.cp` | — | `.expected`, **`.stderr.expected` 78 B** | deep ×3, stdlib (stdout only), evidence | deep — string_view |
| `deep_thread_guard.cp` | — | `.expected`, **`.stderr.expected` 61 B** | deep ×3, stdlib (stdout only), evidence | deep — thread guard |
| `deep_thread_nested.cp` | — | `.expected`, `.stderr.expected` (0 B) | deep ×3, stdlib, evidence | deep — nested threads |
| `deep_thread_task5.cp` | — | `.expected`, `.stderr.expected` (0 B) | deep ×3, stdlib, evidence | deep — threads |
| `deep_vector_task4.cp` | — | `.expected`, `.stderr.expected` (0 B) | deep ×3, stdlib, evidence | deep — vector allocation atomicity |

## C. `tests/stdlib/compile_time/*.cp` — 52 files in 8 manifests

No `.stdin`, `.expected` or `.stderr.expected` fixtures exist for compile-time
cases. The expectation is the `EXPECT` field of the containing manifest
(`collections.json`, `error.json`, `file.json`, `json.json`, `net.json`,
`string.json`, `thread.json`, `vector.json`). Every case is executed by **ct**
(which runs `c+ check --strict` and `c+ build --strict --sanitize=none`) and by
**evidence** line 32. None is executed by **deep**, **stdlib**, **compiler**,
**runtime**, **safety** or **cov-exec**.

| Manifest | File | EXPECT | stdin / fixtures | Executed by | Classification |
|---|---|---|---|---|---|
| collections | `collections_use_after_drop.cp` | RUNTIME | none | ct, evidence | compile-time ownership — compiler capability gap |
| collections | `collections_use_after_move.cp` | RUNTIME | none | ct, evidence | compile-time ownership — capability gap |
| collections | `collections_dangling_key.cp` | RUNTIME | none | ct, evidence | compile-time ownership — capability gap |
| collections | `collections_mutable_alias.cp` | RUNTIME | none | ct, evidence | compile-time ownership — capability gap |
| collections | `collections_valid.cp` | ACCEPT | none | ct, evidence | compile-time positive control |
| collections | `collections_valid_borrow.cp` | ACCEPT | none | ct, evidence | compile-time positive control |
| error | `error_use_after_reset.cp` | RUNTIME | none | ct, evidence | compile-time ownership — capability gap |
| error | `error_invalid_lifecycle.cp` | REJECT | none | ct, evidence | compile-time negative (tagged) |
| error | `error_null_pointer.cp` | REJECT | none | ct, evidence | compile-time negative (tagged) |
| error | `error_valid.cp` | ACCEPT | none | ct, evidence | compile-time positive control |
| error | `error_valid_reset.cp` | ACCEPT | none | ct, evidence | compile-time positive control |
| file | `file_read_after_close.cp` | RUNTIME | none | ct, evidence | compile-time — capability gap |
| file | `file_write_after_close.cp` | RUNTIME | none | ct, evidence | compile-time — capability gap |
| file | `file_double_close.cp` | RUNTIME | none | ct, evidence | compile-time — capability gap |
| file | `file_use_after_move.cp` | REJECT | none | ct, evidence | compile-time negative (untagged-gap observed) |
| file | `file_invalid_borrow.cp` | REJECT | none | ct, evidence | compile-time negative (tagged) |
| file | `file_valid.cp` | ACCEPT | none | ct, evidence | compile-time positive control |
| file | `file_valid_borrow.cp` | ACCEPT | none | ct, evidence | compile-time positive control |
| file | `file_valid_move.cp` | ACCEPT | none | ct, evidence | compile-time positive control |
| json | `json_valid.cp` | ACCEPT | none | ct, evidence | compile-time positive control |
| json | `json_valid_ref.cp` | ACCEPT | none | ct, evidence | compile-time positive control |
| json | `json_access_after_destroy.cp` | RUNTIME | none | ct, evidence | compile-time — capability gap |
| json | `json_builder_after_destroy.cp` | RUNTIME | none | ct, evidence | compile-time — capability gap |
| json | `json_dangling_ref.cp` | RUNTIME | none | ct, evidence | compile-time — capability gap |
| json | `json_invalid_transfer.cp` | RUNTIME | none | ct, evidence | compile-time — tagging gap |
| net | `net_use_after_close.cp` | RUNTIME | none | ct, evidence | compile-time — capability gap |
| net | `net_double_close.cp` | RUNTIME | none | ct, evidence | compile-time — capability gap |
| net | `net_use_after_transfer.cp` | REJECT | none | ct, evidence | compile-time negative (untagged-gap observed) |
| net | `net_valid.cp` | ACCEPT | none | ct, evidence | compile-time positive control |
| net | `net_valid_borrow.cp` | ACCEPT | none | ct, evidence | compile-time positive control |
| string | `string_use_after_drop.cp` | REJECT | none | ct, evidence | compile-time negative (tagged) |
| string | `string_double_drop.cp` | REJECT | none | ct, evidence | compile-time negative (tagged) |
| string | `string_dangling_view.cp` | RUNTIME | none | ct, evidence | compile-time — downgraded to RUNTIME |
| string | `string_uninitialized.cp` | REJECT | none | ct, evidence | compile-time negative (tagged) |
| string | `string_invalid_transfer.cp` | REJECT | none | ct, evidence | compile-time negative (untagged-gap observed) |
| string | `string_valid_basic.cp` | ACCEPT | none | ct, evidence | compile-time positive control |
| string | `string_valid_view.cp` | ACCEPT | none | ct, evidence | compile-time positive control |
| string | `string_valid_transfer.cp` | ACCEPT | none | ct, evidence | compile-time positive control |
| thread | `thread_double_unlock.cp` | RUNTIME | none | ct, evidence | compile-time — capability gap |
| thread | `thread_unlock_without_ownership.cp` | RUNTIME | none | ct, evidence | compile-time — capability gap |
| thread | `thread_guard_after_destroy.cp` | RUNTIME | none | ct, evidence | compile-time — capability gap |
| thread | `thread_invalid_mutex_owner.cp` | RUNTIME | none | ct, evidence | compile-time — capability gap |
| thread | `thread_valid_guard.cp` | ACCEPT | none | ct, evidence | compile-time positive control |
| thread | `thread_valid_lock.cp` | ACCEPT | none | ct, evidence | compile-time positive control |
| vector | `vector_const_oob.cp` | RUNTIME | none | ct, evidence | compile-time — capability gap |
| vector | `vector_use_after_drop.cp` | RUNTIME | none | ct, evidence | compile-time — capability gap |
| vector | `vector_use_after_move.cp` | RUNTIME | none | ct, evidence | compile-time — tagging gap |
| vector | `vector_mutable_alias.cp` | RUNTIME | none | ct, evidence | compile-time — capability gap |
| vector | `vector_invalid_slice.cp` | RUNTIME | none | ct, evidence | compile-time — capability gap |
| vector | `vector_valid.cp` | ACCEPT | none | ct, evidence | compile-time positive control |
| vector | `vector_valid_slice.cp` | ACCEPT | none | ct, evidence | compile-time positive control |
| vector | `vector_valid_move.cp` | ACCEPT | none | ct, evidence | compile-time positive control |

Declared-expectation totals from the manifests: REJECT 9, ACCEPT 19, RUNTIME 24
= 52 cases.

## D. Fixture files (complete enumeration)

`*.stdin` (11): `deep_file_lines`, `deep_io_1byte`, `deep_io_task2`,
`deep_io_token`, `deep_json_stress`, `eof`, `foundation`,
`interactive_reprompt`, `io_edge_cases`, `multi_tu`, `stdlib_hardening_test`
(each with the `.stdin` suffix). `multi_tu.stdin` is orphaned — see section A.

`*.expected` excluding stderr (42): 28 deep names (section B), plus
`adversarial`, `collections_test`, `containers_safety_test`, `core_extras_test`,
`eof`, `foundation`, `interactive_reprompt`, `io_edge_cases`, `json_test`,
`log_test`, `multi_tu`, `negative`, `safety_regressions`,
`stdlib_hardening_test`.

`*.stderr.expected` (28): exactly the 28 deep names (section B); 25 are 0 bytes
and 3 are non-empty (`deep_string_append`, `deep_string_view`,
`deep_thread_guard`). No non-deep or compile-time test has a `.stderr.expected`.

## E. `tests/libc/test_*.c` relevant to stdlib — 7 files

Selection criterion (objective): the file includes `cplus_libc.h` and/or a
`stdlib/safe/*` header — i.e. it exercises the hardened-libc / safe-wrapper layer
that backs the C+ stdlib. This is a subset of the 97 `test_*.c` files. The full
libc runner executes 85 of the 97 (all except 11 `*_attack.c` files and
`test_corpus_format.c`); the excluded attack/corpus files have their own runners.

| File | stdin fixture | expectation fixture(s) | Executed by | Classification |
|---|---|---|---|---|
| `test_alloc_fault_matrix.c` | — | none; prints `294/294 checks ok` | libc runner (extra `-I.` + SafeCRT lib), `tests/libc/run_alloc_fault_matrix.sh` (evidence line 33) | stdlib-backed — deterministic allocation-failure injection |
| `test_compile_gaps_runtime.c` | — | none; exit code | libc runner | stdlib-backed — runtime coverage of declared compile-time gaps |
| `test_corpus_null.c` | — | none; generated corpus | libc runner and corpus runner (evidence line 26) | stdlib-backed — NULL-argument bug-class corpus |
| `test_corpus_format.c` | — | none; generated corpus | corpus runner (evidence line 26) only; **explicitly skipped** by libc runner (line 11) | stdlib-backed — format-string bug-class corpus |
| `test_format_attack.c` | — | none; exit code | attack runner (evidence line 25); skipped by libc runner | stdlib-backed — format adversarial |
| `test_memory_attack.c` | — | none; exit code | attack runner; skipped by libc runner | stdlib-backed — memory adversarial |
| `test_provenance_attack.c` | — | none; exit code | attack runner; skipped by libc runner | stdlib-backed — provenance adversarial |

## F. Runner gaps found (summary)

1. **No 9-command baseline coverage for the broad stdlib suite.** `run_stdlib_tests.sh`
   is not one of the nine commands and is not called by `run_all_evidence.sh`;
   therefore 21 of the 24 non-deep `*.cp` tests (24 minus the 3 skipped
   `bench_*`) are executed by **none** of the nine baseline commands. They are
   reachable only via `tests/stdlib/run_all.sh`.
2. **`run_stdlib_tests.sh` ignores stderr fixtures.** It compares only
   `<name>.expected`. When it picks up `deep_*.cp` via its glob, the three
   non-empty `.stderr.expected` fixtures are never checked there; byte-exact
   stderr verification exists only in **deep**.
3. **`cov-exec` cannot fail on a bad run** (`|| true`, line 50; output discarded;
   no timeout) and is not a correctness runner. Its four hardcoded cases are a
   coverage probe, not a pass/fail gate.
4. **`runtime` and `safety` runners are outside the evidence aggregate** — neither
   is in the nine commands nor in `run_all_evidence.sh`, so their 3 + 3 hardcoded
   scenarios have no baseline result in `baseline.md`.
5. **4 of the 7 stdlib-relevant libc tests are not in the plain libc runner**
   (`test_corpus_format.c` and the three `*_attack.c` files); they are covered
   only by the corpus/attack runners. Those runners are in `run_all_evidence.sh`
   but not among the nine individually recorded commands.
6. **Orphan fixtures** `multi_tu.stdin` / `multi_tu.expected` are not consumed by
   the `multi_tu` block in `run_stdlib_tests.sh` (inline input, no diff).
