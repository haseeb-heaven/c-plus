# memory_ownership regression suite — Task 2 Phase 6

Owner: subagent 2 of 10, branch `agent/modern-stdlib`.
**Only files under `tests/stdlib/memory_ownership/` were created or modified.**
No header, compiler, runtime, or other test file was edited; no git add/commit.

## Environment

```
cwd  = /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin
compiler = ./build/c+            (in-tree, repo-built)
timeout  = /usr/bin/timeout       (verified with: command -v timeout)
date     = 2026-11-23
```

Linked backend runtime (from `--verbose` link line):
`/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/build-verify-1123/libcplus_runtime.a`.
No live game, Wine, or Windows build is involved — this is a native macOS
strict-mode + UBSan check only.

## Files

| file | purpose |
|---|---|
| `mo_alloc_edges.cp` | zero-size, NULL, alloc/calloc overflow, realloc success/refusal metadata, injected-failure cleanup, telemetry balance, fixed-seed LCG (24 bounded iterations, seed `0x13572468`) |
| `mo_ownership_lifetime.cp` | invalid/double-free refusal via metadata only, UAF detection limits, interior pointers, stale generation, ownership transfer, logging/callback parity, cleanup |
| `mo_alloc_edges.expected`, `mo_alloc_edges.stderr.expected` | byte-exact stdout (62 lines) / empty stderr fixture |
| `mo_ownership_lifetime.expected`, `mo_ownership_lifetime.stderr.expected` | byte-exact stdout (49 lines) / empty stderr fixture |
| `run.sh` | runs strict/no-sanitizer **first**, then UBSan; every build and run wrapped in `timeout -k 5 30`; diffs against fixtures; non-zero on any failure |
| `results/` | captured build logs (empty = clean build), stdout/stderr, empty `.diff` files, runner transcript |
| `NOTES.md` | this file |

Report contract from both programs: one `<tag> <0|1>` per check, then
`MEMORY_OWNERSHIP <checks> <failures>`; exit 0 iff failures == 0.

## Coverage mapping

| requirement | where |
|---|---|
| invalid/double-free refusal via metadata queries (never dereference stale) | `mol_uaf_limit_*`, `mol_df_second_release_refused`, `mol_inv_*` |
| UAF detection limits (documented refusal only) | `mol_uaf_limit_*`; see "Detection limits" below |
| alloc size overflow | `moa_calloc_overflow_*`, `moa_alloc_over_limit_null`, `moa_alloc_size_max_null`, `moa_realloc_over_limit_refused` |
| zero-size | `moa_zero_*`, `moa_calloc_zero_*`, `moa_realloc_zero_one_byte` |
| NULL | `moa_null_*`, `moa_realloc_null_alloc`, `mol_xfer_null_src_refused` |
| interior pointers | `mol_interior_*`, `mol_parity_scenario` interior write |
| realloc failure/success metadata consistency | `moa_realloc_new_*`, `moa_realloc_old_retired`, `moa_realloc_stale_token_refused`, `moa_realloc_refusal_*` |
| stale generation | `moa_realloc_gen_bumped`, `mol_xfer_gen_bumped`, `mol_xfer_old_token_stale`, `mol_xfer_stale_guarded_write` |
| ownership transfer | `mol_xfer_*` (success, NULL source, occupied destination, dst == src) |
| cleanup on failure | `moa_injected_fail_*`, `moa_refusal_cleanup`, `mol_*_cleanup`, `mol_df_live_restored` |
| telemetry balance | `moa_*_balance`, `moa_*_fail_counted`, `mol_*_balance`, `mol_parity_on_balance` |
| logging parity | `mol_parity_off_*`, `mol_parity_on_*`, `mol_parity_results_identical` |

## Verification (strict/no-sanitizer FIRST, then UBSan; timeout 30)

Canonical runner, exact command:

```
sh tests/stdlib/memory_ownership/run.sh > tests/stdlib/memory_ownership/results/suite_run2.stdout 2>&1
echo "runner_rc=$?"
```

Exact output of `cat results/suite_run2.stdout`:

```
BUILD mo_alloc_edges strict_none
RUN   mo_alloc_edges strict_none
PASS mo_alloc_edges strict_none rc=0
BUILD mo_ownership_lifetime strict_none
RUN   mo_ownership_lifetime strict_none
PASS mo_ownership_lifetime strict_none rc=0
BUILD mo_alloc_edges ubsan
RUN   mo_alloc_edges ubsan
PASS mo_alloc_edges ubsan rc=0
BUILD mo_ownership_lifetime ubsan
RUN   mo_ownership_lifetime ubsan
PASS mo_ownership_lifetime ubsan rc=0
MEMORY_OWNERSHIP SUITE PASS
```

`runner_rc=0` (recorded from the `echo` above).

Expanded commands the runner executed, in this exact order:

```
timeout -k 5 30 ./build/c+ build --strict --sanitize=none tests/stdlib/memory_ownership/mo_alloc_edges.cp -o tests/stdlib/memory_ownership/results/mo_alloc_edges_strict_none
timeout -k 5 30 tests/stdlib/memory_ownership/results/mo_alloc_edges_strict_none
timeout -k 5 30 ./build/c+ build --strict --sanitize=none tests/stdlib/memory_ownership/mo_ownership_lifetime.cp -o tests/stdlib/memory_ownership/results/mo_ownership_lifetime_strict_none
timeout -k 5 30 tests/stdlib/memory_ownership/results/mo_ownership_lifetime_strict_none
timeout -k 5 30 ./build/c+ build --strict --sanitize=undefined tests/stdlib/memory_ownership/mo_alloc_edges.cp -o tests/stdlib/memory_ownership/results/mo_alloc_edges_ubsan
timeout -k 5 30 tests/stdlib/memory_ownership/results/mo_alloc_edges_ubsan
timeout -k 5 30 ./build/c+ build --strict --sanitize=undefined tests/stdlib/memory_ownership/mo_ownership_lifetime.cp -o tests/stdlib/memory_ownership/results/mo_ownership_lifetime_ubsan
timeout -k 5 30 tests/stdlib/memory_ownership/results/mo_ownership_lifetime_ubsan
```

No build log, runtime stderr, or `.diff` file is non-empty. All four `.diff`
files are 0 bytes; both `.stderr` captures are 0 bytes.

Final summary lines (identical in both modes, byte-exact fixture match):

```
mo_alloc_edges_strict_none.stdout:        MEMORY_OWNERSHIP 61 0
mo_alloc_edges_ubsan.stdout:              MEMORY_OWNERSHIP 61 0
mo_ownership_lifetime_strict_none.stdout: MEMORY_OWNERSHIP 48 0
mo_ownership_lifetime_ubsan.stdout:       MEMORY_OWNERSHIP 48 0
```

Totals: **2 tests, 109 checks, 0 failures, 4/4 build+run invocations passed,
0 timeouts** (61 + 48 per mode).

Sanitizer instrumentation was confirmed while the binaries were present
(regenerate them with `run.sh` to repeat):

```
nm -u results/mo_alloc_edges_ubsan | grep -i ubsan | head -5
___ubsan_handle_add_overflow_abort
___ubsan_handle_builtin_unreachable
___ubsan_handle_divrem_overflow_abort
___ubsan_handle_out_of_bounds_abort
___ubsan_handle_pointer_overflow_abort
```

The same command on `results/mo_alloc_edges_strict_none` produced no output.
UBSan is a diagnostic here, not a proof of memory safety.

## Self-corrections (real observed failures, not library defects)

The first strict/no-sanitizer run of `mo_alloc_edges.cp` exited 1 with
`MEMORY_OWNERSHIP 61 2`. Both failing checks were **wrong expectations in the
test**, corrected after reading `stdlib/error.hp`:

1. `moa_realloc_null_zero_growth 0` — expected growth delta 2. `stdlib_realloc`
   returns early through `stdlib_alloc` when `ptr == NULL`, so that call does
   not increment `STDLIB_TM_GROWTH`; only the subsequent resize does. Expected
   value corrected to 1, with an explanatory comment.
2. `moa_realloc_refusal_fail_counted 0` — the third conjunct asserted
   `post.alloc_count == pre.alloc_count`, but the block intentionally allocated
   at the start of that section contributes one allocation. Corrected to
   `post.alloc_count - pre.alloc_count == 1ULL`. (The two refusal categories,
   `RANGE` and `INVALID`, both land in `stdlib_note`'s `default` arm, so
   `invalid_ops` delta 2 was correct.)

After those two edits, the same strict/no-sanitizer command exited 0 with
`MEMORY_OWNERSHIP 61 0`; the fixtures were captured from that verified run.
`mo_ownership_lifetime.cp` passed on its first run (`MEMORY_OWNERSHIP 48 0`).

## Detection limits (documented, deliberate — not silently accepted)

- A literal second `stdlib_release(stale)` terminates with
  `CPLUS_EXIT_DOUBLE_FREE` (104); a raw dereference or `cplus_check_live()` on
  a released pointer terminates with `CPLUS_EXIT_TYPE_CONFUSION` (105). The
  suite **never** invokes those fatal paths. Double-free / invalid-free / UAF
  are detected only through non-destructive metadata queries and the
  `stdlib_transfer_ownership` refusal, which is what `mol_uaf_limit_*` records.
- Interior pointers are not owner handles: `stdlib_meta_find` is exact-address
  only, so an interior address reports size 0 / generation 0 and its transfer
  is refused. Valid interior access must go through the guarded `(base, offset)`
  API (`stdlib_mem_set`), which is also exercised.
- `stdlib_realloc` refusal (over-limit or untracked pointer) is failure-atomic:
  original size, generation and payload are unchanged, verified by
  `moa_realloc_refusal_*`.
- `cplus_realloc`/`stdlib_realloc` old-block raw ownership is **not** asserted
  here: the linked runtime is an external prebuilt archive, and this suite
  deliberately does not query runtime-internal metadata on a retired address.

## Reproduced defects

None in the stdlib/runtime under test. The only two failures observed during
this task were the two incorrect test expectations documented above and were
fixed in the test files. No pass is fabricated: every number in this file
comes from the recorded commands and the captured `results/` logs.
