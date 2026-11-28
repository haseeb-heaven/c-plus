# Task 3 — `stdlib/error.hp` as the error propagation hub (system APIs)

## Owned files (and only these)

| File | Role |
|---|---|
| `tests/stdlib/system_safety/error_hub_core.cp` | Core error-object semantics: A construct/clear/reset, B truncation boundaries incl. NULL, C code→category mapping, D single-TU reentrancy, E telemetry on/off parity, F 128 bounded cycles |
| `tests/stdlib/system_safety/error_hub_system.cp` | System-API propagation: P1 io/file/fs/json/net/thread mapping, P2 failed file open → JSON parse → network close chain, P3 8-failure telemetry+callback parity, P4 32 bounded cycles |
| `tests/stdlib/system_safety/error_hub_multi_main.cp` | Multi-TU main for the callback recursion-guard probe (TU B) |
| `tests/stdlib/system_safety/error_hub_multi_cb.cp` | Multi-TU callback definition (TU A) that re-enters `error_fail` |
| `tests/stdlib/system_safety/error_NOTES.md` | This file |
| `stdlib/error.hp` | **Edited only because a defect was reproduced** (see FINDING 1) |

No other file was created, edited, added or committed by this agent. No compiler
or runtime file was modified. All binaries/logs were written to the pre-approved
temp dir `/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/eh_work`.

## Counts

- Test source files: **4** (+ this notes file).
- Checks printed: `ERROR_HUB_CORE 45 0` (45 checks), `ERROR_HUB_SYSTEM 27 0`
  (27 checks), `MULTI_TU_GUARD`/`MULTI_TU_FINAL` (2 checks) = **74 checks total,
  0 failures** after the fix.
- Library defects reproduced: **1** (multi-TU recursion guard, fixed).
- Test-side expectation bugs found and corrected in the test itself: **1**
  (no header edit).
- Builds: 6 (3 programs × 2 modes after the fix) + 2 UBSan attempts before the
  workaround + 2 regression builds. All post-fix builds/runs rc 0.
- Run time: every build and every run completed under `timeout 30` (wall clock
  well under 1 s per test).

## Coverage vs. requested items

| Requested coverage | Where |
|---|---|
| constructing / clearing / resetting error | `ehc_a_fresh`, `_set`, `_clear`, `_reset`, `_null_helpers`, `_explicit_category`, `_category_override`, `_reuse_after_clear` |
| exact message truncation incl. NULL | `ehc_b_len_0/1/254/255/256/257/300`, `ehc_b_null_message`, `ehc_b_empty_message`, `ehc_b_no_stale_tail` (255-byte cap + NUL at `text[255]`) |
| category/code mapping across io/file/fs/json/net/thread | `ehc_c_code_*` + `ehc_c_names` for all `cplus_exit_code` values; `ehs_p1_io_null_buf`, `_fs_null_path`, `_fs_missing`, `_file_read_closed`, `_file_close_twice`, `_json_parse_bad`, `_json_parse_file_missing/null`, `_net_closed_read/write`, `_thread_start_null`, `_thread_join_unjoinable` |
| callback reentrancy and recursion guard | `ehc_d_no_recursion`, `_outer_wins`, `_no_latch`, `_null_msg_to_callback`, `_nested_counters`, `_callback_cleared`; multi-TU probe (`FINDING 1`) |
| telemetry on/off parity | `ehc_e_*` (6-failure sequence, byte-identical records, zero counters off, exact counters on, re-enable, disable-again), `ehs_p3_record_parity/_off_zero/_on_exact/_callback_on/_callback_off` |
| failed file open → JSON parse → network close | `ehs_p2_seeded_long`, `_file_error_replaces_seed`, `_json_error_replaces_file`, `_net_error_replaces_json`, `_after_drop_unchanged`, `_success_clears` |
| repeated failure/recovery cycles | `ehc_f_128_cycles` (128 fixed-pattern iterations), `ehs_p4_32_cycles` (32 iterations, 3-way fixed pattern) |
| no stale state after failures | `ehc_b_no_stale_tail`, `ehc_f_final_clean`, `ehs_p1_net_drop_no_stale`, `ehs_p2_after_drop_unchanged`, `ehs_p2_success_clears`, `ehs_p4_final_clean` |

All loops are bounded with fixed iteration counts (128, 32, 7, 6, 8) and all
inputs are literal strings. There is no RNG, clock, stdin, network peer, or
environment-dependent input; the "fixed seed" is the literal input sequence.

## FINDING 1 (real defect, reproduced and fixed) — recursion guard was per-TU

`stdlib_note()` used a function-local `static __thread int reporting`. Because
`stdlib_note()` is `static inline` in a header, every translation unit got its
own guard. A callback installed in TU A re-entering `error_fail()` from TU A
was **not** suppressed when the outer failure came from TU B, despite the
header's stated "per-thread re-entrancy guard drops reports raised from inside
the callback" contract.

Minimal repro (`error_hub_multi_main.cp` + `error_hub_multi_cb.cp`, before fix,
`--strict --sanitize=none`):

```
MULTI_TU_GUARD 2 1          # expected 1 0
MULTI_TU_FINAL 1 109
exit 1
```

The callback ran twice, with one nested invocation that should have been
dropped. Fix in `stdlib/error.hp` (only file outside the test dir that was
edited): replace the `static __thread` local with a single weak file-scope TLS
variable, `__attribute__((weak)) __thread int stdlib_error_reporting;`, and
use it in `stdlib_note()`. Weak TLS definitions merge across TUs, preserving
per-thread semantics. The orchestrator committed this header edit at `664af8b`;
this agent performed no `git add` / `git commit`.

After fix, both modes print `MULTI_TU_GUARD 1 0` and exit 0. Existing error
tests were re-run to check for regressions: `deep_error_task5` →
`ERROR_TASK5 22 0`, `extra_error_metadata` → `EXTRA_ERROR_METADATA 75 0`, both
rc 0, empty stderr (`--strict --sanitize=none`).

## Test-side correction (not a library defect)

First draft of `ehc_d_null_msg_to_callback` expected the callback to receive
`NULL` when `error_fail_cat(..., NULL)` was called. The contract is the
opposite: `stdlib_note()` normalises NULL to `""`. The check was corrected to
expect a non-NULL zero-length message; no header edit was involved. After the
correction the core suite went from `ERROR_HUB_CORE 45 1` to `45 0`.

## Environment observation (not an `error.hp` defect)

A plain `--strict --sanitize=undefined` build initially failed before any test
source was compiled:

```
/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/cplus_runtime.c:129:23:
  error: use of undeclared identifier 'CPLUS_EXIT_ARITHMETIC'
error: failed to compile instrumented C+ runtime
```

The instrumented runtime source (sibling `c-plus-core`) now requires an enum
value absent from this repo's stale `runtime/include/cplus/cplus.h`. Editing
runtime/compiler files is explicitly out of scope, so UBSan was verified with a
read-only environment override:

```
export CPLUS_INCLUDE=/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/include
```

With that override (no file changes), all UBSan builds/runs pass with empty
stderr and output byte-identical to strict/no-sanitizer. `CPLUS_RUNTIME_LIB` was
not needed (the local `build/libcplus_runtime.a` links fine).

`net.hp` has no `close(error*)`; the only close-like method is
`connection::drop()` with no `error*` argument. The reachable "network close"
failure path is therefore a read/write on a closed/invalid (`connection_borrow(-1)`)
connection, followed by `drop()` — covered by `ehs_p1_net_*` and `ehs_p2_*`.
This is an API observation, not a defect.

## Exact commands and results (run from repository root)

Environment for every command:

```sh
export PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin
export CPLUS_CLANG=/usr/bin/clang
ROOT=/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
TMPD=/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/eh_work
```

STRICT / `--sanitize=none` — run FIRST, as required:

```sh
timeout 30 "$ROOT/build/c+" build "$ROOT/tests/stdlib/system_safety/error_hub_core.cp" --strict --sanitize=none -o "$TMPD/error_hub_core_strict_none"
timeout 30 "$TMPD/error_hub_core_strict_none" > "$TMPD/core.strict_none.out" 2> "$TMPD/core.strict_none.err"

timeout 30 "$ROOT/build/c+" build "$ROOT/tests/stdlib/system_safety/error_hub_system.cp" --strict --sanitize=none -I"$ROOT/third_party/yyjson" -L"$ROOT/build/ext" -lyyjson -o "$TMPD/error_hub_system_strict_none"
timeout 30 "$TMPD/error_hub_system_strict_none" > "$TMPD/system.strict_none.out" 2> "$TMPD/system.strict_none.err"

timeout 30 "$ROOT/build/c+" build "$ROOT/tests/stdlib/system_safety/error_hub_multi_main.cp" "$ROOT/tests/stdlib/system_safety/error_hub_multi_cb.cp" --strict --sanitize=none -o "$TMPD/error_hub_multi_strict_none"
timeout 30 "$TMPD/error_hub_multi_strict_none" > "$TMPD/multi.strict_none.out" 2> "$TMPD/multi.strict_none.err"
```

UBSan (after `export CPLUS_INCLUDE=...` above) — same sources/flags with
`--sanitize=undefined`, outputs `error_hub_core_ubsan`,
`error_hub_system_ubsan`, `error_hub_multi_ubsan` and `*.ubsan.out/err`.
The system test needs the same `-I`/`-L`/`-lyyjson` flags in both modes.
Build emits a harmless `clang: warning: -lyyjson: 'linker' input unused`
from the compile-only step in both modes; linking succeeds and JSON checks pass.

Regression re-run (no edits to these files):

```sh
timeout 30 "$ROOT/build/c+" build "$ROOT/tests/stdlib/deep_error_task5.cp" --strict --sanitize=none -o "$TMPD/reg_deep_error_task5"
timeout 30 "$ROOT/build/c+" build "$ROOT/tests/stdlib/agent2/extra_error_metadata.cp" --strict --sanitize=none -o "$TMPD/reg_extra_error_metadata"
```

### Result matrix (all after the header fix)

| Program | Mode | Build rc | Run rc | Final / key output | stderr |
|---|---|---|---|---|---|
| `error_hub_core.cp` | strict / none | 0 | 0 | `ERROR_HUB_CORE 45 0` | 0 bytes |
| `error_hub_core.cp` | strict / UBSan | 0 | 0 | `ERROR_HUB_CORE 45 0` | 0 bytes |
| `error_hub_system.cp` | strict / none | 0 | 0 | `ERROR_HUB_SYSTEM 27 0` | 0 bytes |
| `error_hub_system.cp` | strict / UBSan | 0 | 0 | `ERROR_HUB_SYSTEM 27 0` | 0 bytes |
| multi-TU main+cb | strict / none | 0 | 0 | `MULTI_TU_GUARD 1 0` / `MULTI_TU_FINAL 1 109` | 0 bytes |
| multi-TU main+cb | strict / UBSan | 0 | 0 | `MULTI_TU_GUARD 1 0` / `MULTI_TU_FINAL 1 109` | 0 bytes |
| `deep_error_task5.cp` | strict / none | 0 | 0 | `ERROR_TASK5 22 0` | 0 bytes |
| `extra_error_metadata.cp` | strict / none | 0 | 0 | `EXTRA_ERROR_METADATA 75 0` | 0 bytes |

Before-fix multi-TU (documented above): build rc 0, run rc 1,
`MULTI_TU_GUARD 2 1`. Before-fix core (with the wrong test expectation):
`ERROR_HUB_CORE 45 1`.

Mode parity: `diff -u` of strict/no-san vs UBSan stdout is clean for all three
programs. MD5s (identical per program across modes):

```
core:   8771c00bed4a722f0a363967afbf9f7c  (46 lines)
system: 4ae312571034044ea999f79106d2e7c6  (28 lines)
multi:  39e43a46096ec61e523f572be83d9cca  (2 lines)
```

## Exact stdout (all three programs, both modes)

`error_hub_core.cp` (46 lines; every check `1`):

```
ehc_a_fresh 1
ehc_a_set 1
ehc_a_clear 1
ehc_a_reset 1
ehc_a_null_helpers 1
ehc_a_explicit_category 1
ehc_a_category_override 1
ehc_a_reuse_after_clear 1
ehc_b_len_0 1
ehc_b_len_1 1
ehc_b_len_254 1
ehc_b_len_255 1
ehc_b_len_256 1
ehc_b_len_257 1
ehc_b_len_300 1
ehc_b_null_message 1
ehc_b_empty_message 1
ehc_b_no_stale_tail 1
ehc_c_code_ok 1
ehc_c_code_io 1
ehc_c_code_resource 1
ehc_c_code_bounds 1
ehc_c_code_ownership 1
ehc_c_code_uaf 1
ehc_c_code_double_free 1
ehc_c_code_unsupported 1
ehc_c_code_nullptr 1
ehc_c_code_type_confusion 1
ehc_c_code_unknown 1
ehc_c_names 1
ehc_d_no_recursion 1
ehc_d_outer_wins 1
ehc_d_no_latch 1
ehc_d_null_msg_to_callback 1
ehc_d_nested_counters 1
ehc_d_callback_cleared 1
ehc_e_enabled_flag 1
ehc_e_disabled_flag 1
ehc_e_record_parity 1
ehc_e_off_zero 1
ehc_e_on_counters 1
ehc_e_reenable 1
ehc_e_off_again 1
ehc_f_128_cycles 1
ehc_f_final_clean 1
ERROR_HUB_CORE 45 0
```

`error_hub_system.cp` (28 lines; every check `1`):

```
ehs_p1_io_null_buf 1
ehs_p1_fs_null_path 1
ehs_p1_fs_missing 1
ehs_p1_file_open_invalid 1
ehs_p1_file_read_closed 1
ehs_p1_file_close_twice 1
ehs_p1_json_parse_bad 1
ehs_p1_json_parse_file_missing 1
ehs_p1_json_parse_file_null 1
ehs_p1_net_closed_read 1
ehs_p1_net_closed_write 1
ehs_p1_net_drop_no_stale 1
ehs_p1_thread_start_null 1
ehs_p1_thread_join_unjoinable 1
ehs_p2_seeded_long 1
ehs_p2_file_error_replaces_seed 1
ehs_p2_json_error_replaces_file 1
ehs_p2_net_error_replaces_json 1
ehs_p2_after_drop_unchanged 1
ehs_p2_success_clears 1
ehs_p3_record_parity 1
ehs_p3_off_zero 1
ehs_p3_on_exact 1
ehs_p3_callback_on 1
ehs_p3_callback_off 1
ehs_p4_32_cycles 1
ehs_p4_final_clean 1
ERROR_HUB_SYSTEM 27 0
```

multi-TU probe (2 lines, both modes):

```
MULTI_TU_GUARD 1 0
MULTI_TU_FINAL 1 109
```

## Non-goals / limitations

- No thread-concurrency stress (callback called from multiple OS threads): the
  guard is now TLS and shared, but this suite only exercises one thread plus
  the cross-TU case. The `stdlib_state` counters remain atomics.
- No self-referential message (`error_fail_cat(&slot, ..., slot.what())`): that
  would risk overlapping `memcpy` inside `error_fail_cat` and is outside the
  requested contract; not asserted here.
- No live network peer, real socket reset, or fatal-probe test; invalid-handle
  paths are deterministic and bounded.
- No claim of live-game or production success; results are build + emulator-
  free host execution only.
