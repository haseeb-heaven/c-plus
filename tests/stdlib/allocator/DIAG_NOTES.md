# Task 5 Phase 3 — runtime allocation diagnostics: verification notes

**Date:** 2026-11-23
**Branch:** `agent/modern-stdlib`
**Repo root:** `/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib`
**Compiler:** `build/c+` (pre-existing binary, not rebuilt by this task)
**Timeout:** `/usr/bin/timeout -k 5 30` on every build and every run

## File ownership (as instructed)

Created/modified **only** `tests/stdlib/allocator/diag_*.cp` and this
`DIAG_NOTES.md`. No `stdlib/error.hp` change was needed (no reproduced defect in
that header required a fix within this task's scope). No compiler, runtime, or
header edits. No `git add` / `git commit` was run by this agent.

> **Repo-state collision (important):** two other agents committed this whole
> directory while this work was in progress (`960c997`, `664af8b`), including
> these three in-progress test files. The committed snapshot of
> `diag_identity.cp` still contains a temporary `cplus_alloc_bytes()` probe
> that terminates the process (`C+ runtime error: invalid length query`). The
> **working-tree** copy below is the verified final version with that probe
> removed; it differs from HEAD by exactly those 5 lines. Use the working-tree
> files. This agent did not stage or commit anything.

## Files

| File | Purpose |
| --- | --- |
| `diag_identity.cp` | Allocation identity beyond address (generation + metadata), size queries, live state, reallocation history via generation, allocation-site SKIP |
| `diag_liveness.cp` | Live→retired state, invalid-free refusal (metadata path), double-free refusal (idempotent API cleanup), ownership-transfer invalidation |
| `diag_telemetry.cp` | Leak signal with telemetry on/off, memory usage statistics, failure diagnostics via the error callback |

## API inspection before authoring (no duplication)

Existing APIs were read first and reused, not reimplemented:

- `stdlib/error.hp`: checked allocator (`stdlib_alloc`, `stdlib_calloc`,
  `stdlib_realloc`, `stdlib_release`), side-table metadata queries
  (`stdlib_alloc_size`, `_elem_size`, `_length`, `_generation`,
  `_check_generation`, `_query_length`), `stdlib_safe_div`, `stdlib_state_t`,
  telemetry (`stdlib_telemetry_set_enabled/_reset/_snapshot`, ten counters),
  `stdlib_note`, `stdlib_set_error_callback`, error categories, `error` class,
  `stdlib_transfer_ownership`.
- `stdlib/memory.hp`: `stdlib_mem_capacity/_count/_token/_check/_check_range/
  _copy/_move/_set/_borrow/_transfer/_release`.

Nothing in `error.hp` was changed. The tested behaviour is exactly what those
headers document: metadata queries read a checked side table and never touch
user bytes; a genuine second `cplus_release` on a raw address is fatal by
design (`CPLUS_EXIT_DOUBLE_FREE`), so the suite exercises only the refusal
paths that fail **before** any deallocation.

## Output protocol

`<tag> <1|0>` per check (1 = pass); optional `SKIP <tag> <reason>`; final line
`ALLOC_DIAG <checks> <failures>`. Exit status is 1 iff failures > 0. SKIP lines
are not counted as checks. All loops are bounded and fixed, so output is
deterministic; reaching the final line under the external timeout proves no
infinite loop. No pointer address is ever printed, so strict and UBSan output
cannot differ on allocator layout.

## Requirement → coverage

| Required coverage | Tags |
| --- | --- |
| Allocation identity beyond address (generation + metadata) | `id_identity_generation`, `id_gen_monotonic`, `lv_dead_state`, `lv_transfer_ok` |
| Size queries | `id_alloc_size`, `id_calloc_layout`, `id_query_explicit`, `id_query_default`, `id_query_wider`, `id_query_null`, `id_mem_wrappers`, `tm_usage_per_block` |
| State / liveness | `id_live_check`, `lv_live_state`, `lv_dead_state`, `lv_untracked_metadata` |
| Invalid-free refusal | `lv_untracked_metadata`, `lv_untracked_realloc_refused`, `lv_untracked_realloc_diagnosed`, `lv_transfer_untracked` |
| Double-free refusal | `lv_mem_release_nulls`, `lv_double_release_noop`, `lv_null_release_noop` |
| Leak detection via telemetry on/off | `tm_on_leak_visible`, `tm_on_balanced`, `tm_on_no_leak`, `tm_off_zero`, `tm_off_not_backfilled`, `tm_off_free_counted` |
| Reallocation history via generation | `id_realloc_new_identity`, `id_realloc_stale_refused`, `id_realloc_fresh_ok`, `id_realloc_chain`, `id_realloc_oversize_refused`, `id_realloc_growth_counter`, `id_realloc_no_growth_on_refusal` |
| Allocation-site tracking | `SKIP id_alloc_site no_host_site_metadata` (reason below) |
| Ownership transfer invalidation | `lv_transfer_ok`, `lv_transfer_stale_token`, `lv_transfer_null_handles`, `lv_transfer_untracked`, `lv_transfer_self` |
| Memory usage statistics | `tm_on_counts`, `tm_usage_bytes`, `tm_usage_totals`, `tm_usage_per_block`, `tm_usage_balanced`, `tm_cb_on_counters` |

## SKIP reason (allocation-site tracking)

`stdlib_meta_slot` in `stdlib/error.hp` (lines 102–107) contains exactly four
fields: `ptr`, `byte_size`, `elem_size`, `generation`. There is no file, line,
function, stack-trace or site field, and neither `memory.hp` nor `error.hp`
exposes any allocation-site query. Host metadata therefore cannot answer
"where was this block allocated", so no assertion is possible. Printed as
`SKIP id_alloc_site no_host_site_metadata`; not counted as a pass, not treated
as a defect.

## Verification 1 — strict / no sanitizer (run FIRST)

Exact commands, executed from the repo root with
`OUT=/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/diag_artifacts`:

```sh
mkdir -p "$OUT"
for t in diag_identity diag_liveness diag_telemetry; do
  timeout -k 5 30 build/c+ build tests/stdlib/allocator/$t.cp \
    --strict --sanitize=none -o "$OUT/${t}_strict_none" \
    > "$OUT/${t}_strict_none.build.log" 2>&1
  timeout -k 5 30 "$OUT/${t}_strict_none" </dev/null \
    > "$OUT/${t}_strict_none.out" 2> "$OUT/${t}_strict_none.err"
done
```

Results (all builds rc=0, all runs rc=0, all stderr empty):

| Test | Final stdout line | Checks | Failures |
| --- | --- | ---: | ---: |
| `diag_identity` | `ALLOC_DIAG 18 0` | 18 | 0 |
| `diag_liveness` | `ALLOC_DIAG 14 0` | 14 | 0 |
| `diag_telemetry` | `ALLOC_DIAG 16 0` | 16 | 0 |

**Strict/no-sanitizer total: 48 checks, 0 failures, 3/3 builds and runs rc=0,
1 SKIP.**

## Verification 2 — UBSan (run AFTER strict/no-sanitizer)

### Reproduced toolchain defect (external to this task)

The first UBSan build attempt failed before reaching any test code:

```
$ timeout -k 5 30 build/c+ build tests/stdlib/allocator/diag_identity.cp \
    --strict --sanitize=undefined -o ... 
/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/cplus_runtime.c:129:23: error:
  use of undeclared identifier 'CPLUS_EXIT_ARITHMETIC'
error: failed to compile instrumented C+ runtime
BUILD_RC=1  (identical for all three test files)
```

Cause: the sibling repo `c-plus-core` (modified by another agent at 09:03–09:04,
`git status` shows uncommitted changes) added `CPLUS_EXIT_ARITHMETIC` to its
`runtime/cplus_runtime.c` and `runtime/include/cplus/cplus.h`. The UBSan path
recompiles the runtime source found via `findSourceRoot()` (which resolves to
`c-plus-core`), but the compiler's include search resolves `cplus/cplus.h` to
**this** repo's older header (`c-plus-stdlib/runtime/include/cplus/cplus.h`,
no `CPLUS_EXIT_ARITHMETIC`), so the new enum is undeclared.

A second workaround was tried and rejected:

```
CPLUS_ROOT=/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib ... build ... --sanitize=undefined
ld: symbol(s) not found for architecture arm64
  "___cplus_bounds_check", referenced from: _di_generation_uniqueness
```

This repo's runtime source predates `__cplus_bounds_check`, which the current
compiler emits in strict mode. Not a valid workaround.

### Working verification command (env-only, no file edits)

Point the include search at the sibling header that matches the sibling runtime
source; the toolchain then builds cleanly:

```sh
CPLUS_INCLUDE=/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/include \
  timeout -k 5 30 build/c+ build tests/stdlib/allocator/$t.cp \
    --strict --sanitize=undefined -o "$OUT/${t}_ubsan" \
    > "$OUT/${t}_ubsan.build.log" 2>&1
timeout -k 5 30 "$OUT/${t}_ubsan" </dev/null \
  > "$OUT/${t}_ubsan.out" 2> "$OUT/${t}_ubsan.err"
```

Results (all builds rc=0, all runs rc=0, all stderr empty — no UBSan diagnostics):

| Test | Final stdout line | Checks | Failures |
| --- | --- | ---: | ---: |
| `diag_identity` | `ALLOC_DIAG 18 0` | 18 | 0 |
| `diag_liveness` | `ALLOC_DIAG 14 0` | 14 | 0 |
| `diag_telemetry` | `ALLOC_DIAG 16 0` | 16 | 0 |

**UBSan total: 48 checks, 0 failures, 3/3 builds and runs rc=0, 1 SKIP.**
This is evidence that the *test programs* are UB-free under UBSan; it is not a
statement that the cross-repo build drift is fixed.

## Determinism (byte-identical stdout across modes)

```
cmp -s "$OUT/${t}_strict_none.out" "$OUT/${t}_ubsan.out"
```

All three pairs are identical. SHA-256 (same hash in both modes):

```
37ecf667390a09d43b7399aa48b23b99e459559a02bbf27dbb82f3da0137c13e  diag_identity
18e2935f660c18bf1997891e1de4b3ece079699fce974c8aa1cdd9a64cb730c7  diag_liveness
e03f3b1feabbab06e70734653841f142719a38030f071d95d6c9f6260ccf6b3d  diag_telemetry
```

## Findings

1. **External toolchain defect (reproduced, not fixed here).** Default UBSan
   builds fail with `use of undeclared identifier 'CPLUS_EXIT_ARITHMETIC'`
   because `c-plus-core`'s modified runtime source is compiled against this
   repo's older `cplus.h`. Ownership of the fix is the compiler/runtime repos,
   not the files allowed to this task; no header or runtime was edited.
2. **Unsafe query identified and removed from the tests.** An exploratory
   `cplus_alloc_bytes(old_ptr)` after `stdlib_realloc` terminates with
   `C+ runtime error: invalid length query` (exit 105): that runtime helper is
   fatal for a retired pointer, unlike the checked side-table queries. The
   probe was deleted; all final checks use only non-fatal metadata queries and
   never dereference or query a retired pointer through the fatal API. The
   same probe established that the currently linked runtime releases the old
   block on realloc, so the suspected "realloc leaves old block live" leak was
   **not** reproduced on this toolchain.
3. **No defect reproduced in `stdlib/error.hp` or `stdlib/memory.hp`** within
   the covered behaviours; all 48 checks pass in both modes with empty stderr.
   Known pre-existing findings (e.g. the audit's generation-blind stale-alias
   `ALLOC-01`) were not re-litigated: this suite deliberately stays on the
   metadata/refusal paths so it never performs an actual invalid free.

## Test-authoring corrections (not library defects)

- Strict mode rejected `di_gs[di_i - 1]` (`[CPLUS-BOUNDS] constant array index
  4 is outside the bounds of 'di_gs'`). Replaced with a running `di_prev`
  variable, so no computed array index appears.
- `id_realloc_growth_counter` initially expected +2 growths at a snapshot taken
  *after* both successful reallocs. Corrected: snapshot at function entry
  (+2 total), plus a separate check that the refused resize adds no growth.
- `dt_telemetry_on` originally allocated two blocks before the telemetry reset,
  overwriting their handles. Corrected to allocate only after reset.
- All locals use function-unique prefixes (`di_`, `dl_`, `dt_`) because C+
  lowering resolves same-named locals across functions; no variable division
  or modulo is written in the tests; byte copies use `__builtin_memcpy` where
  relevant.
