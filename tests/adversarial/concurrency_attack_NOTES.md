# Concurrency attack notes

Test: `tests/libc/test_concurrency_attack.c` (standalone C99 + pthreads).
Build/run exactly as specified in the task; runtime ~0.3 s on Apple clang 16 /
arm64. All cross-thread state in the test is mutex- or barrier-protected; the
test itself has no data race or UB.

## FINDING 1 (HIGH): `cplus_realloc` performs the host `realloc()` outside the
metadata lock — stale-slot hijack, EINVAL on live pointers, lost metadata

Reproduced 5/5 runs in attack 1 (8 threads x 2000 iterations, alternating
48-byte <-> ~4 KiB sizes to force host moves). Typical output:

```
FINDING: attack1: N failures [realloc errno EINVAL=N ... originalNoLongerLive=N]
FINDING: attack7: N failures (metaLostOnFail=N ...)
FINDING: attack7: double/invalid-free counters changed during injection
FINDING: attack2: N failures [lc0_lbNZ=... lb_lt_lc=...]
```

Exact interleaving (alloc.c):

1. Thread A: `p = cplus_malloc(48)` -> metadata slot for P is live.
2. Thread A: `cplus_realloc(P, 4096)`; at line 252 it takes `g_meta_lock`,
   reads `old_bytes/old_elem`, passes the pre-check, and **unlocks at line 272**.
3. Thread A: host `realloc(P, 4096)` at line 273 MOVES the block to Q and
   frees P. A's metadata slot for P is still marked live while the lock is
   dropped.
4. Thread B: `cplus_malloc(48)` gets the just-freed address P from the host
   allocator. `meta_insert(P, ...)` at line 119 calls `meta_find(P)`, finds
   A's still-live slot, and takes the `existing != NULL` branch (lines
   124-130): it overwrites A's slot in place, does **not** increment `g_live`,
   so B's allocation is now described by A's slot.
5. Thread A re-acquires the lock (line 278), finds P's slot (now B's) at line
   280. Since `Q != P`, line 288 `meta_remove(s)` tombstones it and line 289
   `meta_insert(Q, ...)` creates a slot for Q. Net: B's allocation P now has
   **no metadata**; `g_live`/`g_live_bytes` are wrong.
6. Thread B: `cplus_realloc(P, ...)` -> `meta_find_addr(P)` returns NULL at
   line 254 -> EINVAL (line 261) and `++g_invalid_frees` (line 259). B sees
   `cplus_alloc_is_live(P) == 0` for a pointer it never freed. Later
   `cplus_free(P)` is also refused/ miscounted.

This explains all four symptoms above with one root cause: EINVAL from realloc
on a caller-live pointer, `originalNoLongerLive`, `lc==0 && lb!=0`, `lb<lc`, and
inconsistent double/invalid-free counters. The counters balance by accident in
some runs, so `live_count` sometimes returns to baseline even while individual
allocations lose their metadata. Fix direction: keep `g_meta_lock` held across
the host `realloc()` (or explicitly quarantine the old address), and do not
treat an existing live slot as a valid target for `meta_insert`.

## FINDING 2 (MEDIUM): `cplus_fclose` false double-close on a fresh raw `FILE*`

attack 5: `cplus_fclose` returned EOF on all 640 freshly opened `tmpfile()`
handles (8 threads x 80). Each stream was opened, written, read back and
closed exactly once; no cross-thread sharing. The 160 `cplus_fopen` paths in
the same attack all closed cleanly, which isolates the defect to streams that
were never registered via `cplus_fopen`.

Interleaving (stdio.c):

1. A `tmpfile()` stream at address P is closed with `cplus_fclose(P)`:
   `was_live == 0`, `cplus_stdio_is_closed(P) == 0`, so line 74
   `cplus_stdio_mark_closed(P)` stores P in `g_closed_streams`.
2. The host allocator reuses P for a brand-new `tmpfile()` handle. Unlike
   `cplus_fopen`, `tmpfile()` never calls `cplus_stdio_unmark_closed`.
3. `cplus_fclose(P)` on the brand-new stream: line 69
   `!was_live && cplus_stdio_is_closed(P)` is true -> returns EOF/EBADF
   **without calling fclose**, leaking the open stream.

The single-threaded reuse probe did not hit an identical address in this
particular allocator run, but the 640 threaded failures are the same path.
Fix direction: clear the closed marker on successful close, and/or do not
reject an unregistered stream solely because its address is in the closed
table.

## FINDING 3 (LOW/CONTRACT): `cplus_transfer_ownership` does not atomically
consume the allocation

attack 3b: two threads with **distinct** source variables (`src_a`, `src_b`)
both holding the same live pointer call `cplus_transfer_ownership` on their
own `(dst,src)` pairs (barrier-synchronised). Both return 0 and both
`dst_a == dst_b == p`. No test-side data race — each thread touches only its
own variables; only the library's `g_meta_lock` is contended.

Cause: the precondition at alloc.c:451-453 checks only the caller's local
`*dst`/`*src`, and the "claim" at lines 457-467 merely bumps a generation and
moves the pointer between caller variables. The metadata still marks the
allocation live with no owner token, so a second claimant passes
`meta_find(*src)` and succeeds. Freeing both owners is a double free. Caveat:
this assumes callers legitimately hold two aliases of one allocation; if that
aliasing is declared out of contract, the API should reject it explicitly.

attack 3a (same `(dst,src)` pair, mutex-protected side first, second side
coordinated by a mutex-protected flag) **held**: protected side rc=0, second
side EINVAL, `free_checked` set dst=NULL, and a genuine second free was
counted exactly once as a double free.

## FINDING 4 (LOW/API): `cplus_alloc_fail_after(-1)` means "fail everything"

`cplus_alloc_fail_after(-1)` makes every subsequent `cplus_malloc` return NULL
until `cplus_alloc_fail_reset()`. alloc.c:505 sets
`g_inject = count < 0 ? 0 : count`, while `inject_fail_locked()` (lines
164-171) treats `-1` as "injection off" and `0` as "fail all". Negative input
is therefore inverted relative to the internal sentinel. Fix direction:
`g_inject = count < 0 ? -1 : count`.

## What held

- attack 1: no crashes, no fill-pattern corruption, `live_count`/`live_bytes`
  returned to baseline after joining (metadata *per-pointer* accuracy is the
  separate Finding 1).
- attack 4 (logging toggle storm, 7 threads x 300 instrumented calls): 0
  return-value failures; `cplus_libc_log_set_enabled` uses an atomic flag.
- attack 5: 160 `cplus_fopen`/`cplus_fclose` unique-path streams across 8
  threads: 0 failures, no read/write cross-contamination.
- attack 6: 8 threads with distinct rounding modes; `cplus_sinf`,
  `cplus_round`, `cplus_sqrt` did not change any thread's mode; main's mode
  unchanged after join.
- attack 7: realloc failure under injection preserved the original pointer's
  contents (0 `dataChangedOnFail`); no host-OOM-induced corruption.
- attack 8: `cplus_swprintf`, `cplus_vswprintf`, `cplus_swscanf` valid paths
  match host results; NULL format/destination and NULL scan string return
  -1/EOF with EINVAL as documented. Note macOS `swprintf` counts the
  terminating NUL in its return value; the test compares against the host
  return rather than hard-coding the ISO count.

## TSan status (honest)

`clang -fsanitize=thread` compiles and links successfully, but **any** run
(abort 134) prints:

```
==PID==ERROR: Interceptors are not working. This may be because ThreadSanitizer
is loaded too late (e.g. via dlopen). Please launch the executable with:
DYLD_INSERT_LIBRARIES=/Library/Developer/CommandLineTools/usr/lib/clang/16/lib/darwin/libclang_rt.tsan_osx_dynamic.dylib
"interceptors not installed" && 0
```

Setting that exact `DYLD_INSERT_LIBRARIES` still fails identically, as does
using `xcrun clang`. Host: arm64-apple-darwin27.2.0, Apple clang 16.0.0.
TSan is therefore unavailable here; all conclusions come from deterministic
pthread runs plus source-level interleaving analysis.

## Compiler warnings

`clang -std=c99 -Wall -Wextra -pedantic -Ilibc -Ilibc/include -fsyntax-only`
is clean (no warnings). The test always returns 0 and prints
`concurrency_attack: ok`, with findings on stdout.
