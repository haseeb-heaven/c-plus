# EDGE100 runtime — results

## Summary

```
EDGE100 runtime 27 104 104 0
EDGE100 interop PASS exit=0 output=INTEROP_OK
```

Pre-fix baseline (recorded before the parent's fix): `EDGE100 runtime 27 104 103 1`.

- **apis** = 27 external `cplus_*` entry points enumerated from
  `runtime/include/cplus/cplus.h` (see `API_MANIFEST.md`) plus 7 header-only
  helpers/macros enumerated there.
- **cases** = 104 fork-isolated cases (39 fatal-path, 65 normal).
- **passed** = 104, **failed** = 0 (post-fix). The pre-fix baseline was 103
  passed / 1 failed; that single finding is documented below and was fixed by
  the parent in `runtime/cplus_runtime.c`.
- No hangs: every child is bounded by `alarm(2)` (exit 124), and the whole
  harness is bounded by `timeout 60`. In this run the harness exited 0 well
  within the bound.

## Reproduction

```sh
sh tests/edge100/runtime/run.sh
```

`run.sh` builds `harness.cp` and the C/C++ interop TUs against **this**
repository's `runtime/include/cplus/cplus.h` and `runtime/cplus_runtime.c`.
It always exits 0 on a successful build+run; verdicts are on stdout. Raw
artifacts (per-case stdout/stderr, build logs, binary) are under
`tests/edge100/runtime/out/` (gitignored workspace output).

Toolchain used: `/usr/bin/clang` 16.0.0 (Apple), `-std=gnu17`, host `arm64`.
The `build/c+` driver was deliberately **not** used: it delegates to the
separate `c-plus-core` repo and links a different runtime, which would not
test the ABI this repository ships.

## Coverage

Covered dimensions (full matrix in `API_MANIFEST.md`): valid, NULL, invalid
handle, double cleanup, nested scopes, fatal path via subprocess, and the
error-code domain (all nine codes 101-109). For every fatal case the parent
verifies both the exact deterministic exit code and the presence of the
`C+ runtime error:` line in the child's captured stderr.

## Finding — observed pre-fix, resolved by parent

**`cleanup_fatal_double_free` — managed scope cleanup with a NULL `drop`
silently skips `cplus_release`, so a double-managed release is not detected.**

- Expected (per `cplus.h`: `CPLUS_CLEANUP_MANAGED /* managed memory:
  cplus_release() */`): pushing the same managed pointer twice onto a scope
  and calling `cplus_scope_cleanup` should call `cplus_release` twice, the
  second call terminating with `CPLUS_EXIT_DOUBLE_FREE` (104).
- Observed: child returned the harness sentinel 250 (i.e. cleanup returned
  normally); the per-case stderr file is empty (0 bytes) — no fatal, no
  diagnostic.
- Root cause: `runtime/cplus_runtime.c`, `cplus_scope_cleanup()` (~line 349):

  ```c
  if (c->ptr && c->drop) {                 /* <-- requires drop != NULL */
    if (c->kind == CPLUS_CLEANUP_MANAGED)
      cplus_release(c->ptr);
    else
      c->drop(c->ptr);
  }
  ```

  A `CPLUS_CLEANUP_MANAGED` record with `drop == NULL` is the natural caller
  usage (the compiler's generated scope records for managed allocations pass
  NULL; the same pattern appears in `c-plus-core`'s own generated API-coverage
  scenarios), yet the guard skips it entirely. The same defect means
  `cleanup_managed` does not actually release its allocation when `drop` is
  NULL.

- Control that isolates the trigger: `cleanup_managed_drop_uaf` pushes a
  managed pointer with a **non-NULL** `drop`, cleans up, then calls
  `cplus_check_live` — it correctly exits 105 (pointer dangling), proving the
  `CPLUS_CLEANUP_MANAGED` -> `cplus_release` path itself works once the
  `c->drop` guard is satisfied. This case passes. The bug is the spurious
  `c->drop` requirement, and it is present in the same form in `c-plus-core`'s
  runtime, so it is an upstream contract issue, not local corruption.
- Impact: managed allocations registered via `cplus_scope_push(..., NULL,
  CPLUS_CLEANUP_MANAGED)` are leaked at scope exit, and a double-registration
  goes undetected instead of producing the documented deterministic
  double-free failure. No memory corruption occurs.

This was reported as a finding rather than fixed by this agent: the task's
file ownership forbids editing `runtime/` sources.

### Resolution (parent, post-handoff)

The parent applied the fix in `runtime/cplus_runtime.c` — the only source
change; this agent made **no** source edits and performed **no** git
operations. In `cplus_scope_cleanup()` the guard now reads:

```c
if (c->ptr && (c->drop || c->kind == CPLUS_CLEANUP_MANAGED)) {
```

so a `CPLUS_CLEANUP_MANAGED` record with a NULL `drop` still reaches
`cplus_release()` (the original comment above line 352 documents the EDGE100
finding). Post-fix verification:

```
EDGE100 runtime 27 104 104 0
EDGE100 interop PASS exit=0 output=INTEROP_OK
```

All 104 cases now pass, including `cleanup_fatal_double_free`, which exits 104
with the `C+ runtime error:` diagnostic. The parent additionally reports
**ABI MATCH** and **CRT 86/86** for the same change.

## All cases passed

All 104 cases include the complete error-code domain sweep, all
bounds/NULL/invalid-handle/double-cleanup/nested-scope paths, full managed
allocation lifecycle (`alloc`/`zalloc`/`alloc_array`/`realloc`/`release`/
`alloc_bytes`/`alloc_count`/`check_live`/`check_index`/`zero`), scope LIFO and
idempotence, mutex/TLS NULL-tolerance and lifecycle, all print kinds and exact
output, scanned scalar/string values and their 108/109 error paths, and the
header-only rotate/move/new/countof helpers. C/C++ interop compiled and ran
cleanly.
