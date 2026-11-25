# Adversarial allocator attack — results (`test_alloc_attack.c`)

All probes go through defined public interfaces. No freed pointer is ever
dereferenced, and `cplus_free` is never called twice on the same address;
double-free probes use a saved alias through `cplus_free_checked`.

## Verification performed

```
sh tests/libc/build_libc.sh >/dev/null
clang -std=c99 -Wall -Wextra -pedantic -Ilibc -Ilibc/include \
      tests/libc/test_alloc_attack.c build/libc/libcplus_hardened.a \
      -lm -pthread -o /tmp/test_alloc_attack && /tmp/test_alloc_attack
# same plus -fsanitize=undefined -fno-sanitize-recover=all
```

- Plain build: **0 warnings**, exit 0, prints `alloc_attack: ok` (~0.34 s).
- UBSan build: exit 0, no UBSan diagnostics, prints `alloc_attack: ok` (~0.44 s).
- Finding count stable across 10 consecutive runs (5/5 each time).
- ASan could not be used: even a trivial `int main(void){return 0;}` built with
  `-fsanitize=address` aborts here with
  `AddressSanitizer: CHECK failed: sanitizer_malloc_mac.inc:189 ... asan_init_is_running`.
  This is an environment/ASan-runtime problem, **not** a library finding.

## Attacks that HELD (hard assertions)

1. **Overflow shapes**: `cplus_malloc(SIZE_MAX)`, `cplus_calloc(SIZE_MAX,2)`,
   `cplus_calloc(2,SIZE_MAX)`, `cplus_calloc(SIZE_MAX,1)`, and
   `cplus_realloc(p,SIZE_MAX)` all return `NULL` with `errno == ENOMEM`, no crash.
   A failed `realloc` preserves the original pointer, its 64-byte metadata and
   all 64 payload bytes.
2. **Failure injection**: `fail_after(0)` fails the next attempt; `fail_after(1)`
   allows one success then fails. `calloc` under injection returns `NULL`/
   `ENOMEM` with no live-count/live-byte drift. `fail_reset()` restores normal
   allocation. Original pointer + contents + counters survive injected and
   host-OOM `realloc` failures.
3. **Zero size**: `malloc(0)`, `calloc(0,n)`, `realloc(p,0)` all return live
   allocations with consistent metadata (1 backing byte each); no crash/leak.
4. **Free diagnostics (happy path)**: `cplus_free_checked` frees a live pointer
   and NULLs the out-param; a double-free via a saved alias returns `EINVAL`,
   sets `errno == EINVAL`, and increments `double_free_count` by exactly 1;
   a stack address increments `invalid_free_count` by exactly 1; `NULL` inout
   and `NULL` pointee are handled as defined.
5. **Metadata queries**: fresh host `malloc` pointers and interior pointers
   report `is_live == 0`, `size == 0`, `get_meta` returns `EINVAL` with a fully
   zeroed output struct; freed cplus pointers also report dead/zeroed.
   NULL out-params are rejected with `EINVAL`.
6. **Generation/ownership**: transfer clears the source, bumps the generation,
   invalidates the old generation, and rejects all second-transfer forms
   (NULL src, non-NULL dst, `dst == src`, NULL params).
7. **Concurrency**: 4 threads x 1500 alloc/memset/query/free iterations; while
   they run, `live_count` stayed within `[base, base+4]` and `live_bytes` within
   `[base_bytes, base_bytes+156]`; after join both counters returned exactly to
   baseline. No crash, no underflow.
8. **Logging**: toggling `cplus_libc_log_set_enabled(1/0)` around every failure
   path (overflow, injected `calloc` failure, failed `realloc`, double free,
   invalid metadata query, invalid ownership transfer) produced identical
   return values and `errno`.

## Confirmed BREAKS (guarded + `FINDING:` in the test)

### A — False double-free on a recycled host address (`cplus_free_checked`)

Sequence:
```
void *p = cplus_malloc(64);
void *a = p; cplus_free_checked(&a);       /* ok, host frees p */
void *h = malloc(64);                       /* h == p on this platform */
void *hc = h; int rc = cplus_free_checked(&hc);
```
Observed: `rc == EINVAL`, `double_free_count += 1`, `invalid_free_count += 0`,
and `h` is **not** freed by cplus (the caller must free it). A brand-new host
allocation is reported as a double free.

### A2 — Same false positive through `cplus_realloc`

After `cplus_realloc(NULL,40)` then `cplus_free(rn)`, `void *h = malloc(40)`
reuses `rn`'s address; `cplus_realloc(h, 80)` returns `NULL`/`EINVAL` and
increments `double_free_count` (instead of `invalid_free_count`) for that
legitimate host pointer. Stable across 10 runs.

Root cause for A/A2: `g_recent` is a 1024-entry direct-mapped cache keyed only
by `meta_hash(p) & 1023`; an entry is never invalidated when the allocator
recycles the address, so an address match is treated as proof of a prior cplus
free.

### B — Real double free misclassified as invalid free (recent-cache collision)

Two distinct live cplus allocations `pa`, `pb` whose `meta_hash & 1023` collide
(found deterministically by scanning 8192 allocations with the mirrored hash).
```
cplus_free_checked(&alias_of_pa);  /* ok: sets recent[idx] = pa */
cplus_free_checked(&alias_of_pb);  /* ok: overwrites recent[idx] = pb */
cplus_free_checked(&alias_of_pa);  /* real double free */
```
Observed: `double_free_count += 0`, `invalid_free_count += 1`. A caller that
relies on `double_free_count` to detect double frees misses this entirely.
This is a genuine detection bypass, not just a cosmetic diagnostic issue.

### C / C2 — False "live" claim and permanent accounting leak after host-side free

```
void *q = cplus_malloc(48);
free(q);                            /* host free bypasses cplus bookkeeping */
cplus_alloc_is_live(q);             /* returns 1 */
```
The side table is not updated because the host `free` never goes through
cplus. Consequences: `cplus_alloc_is_live` returns a false "live" claim
(violates the "no false live claim on freed/host pointers" requirement), and
`live_count`/`live_bytes` are permanently inflated by 1 / 48 bytes. The test
asserts exactly this residual drift (`stale_live=1, stale_bytes=48`) rather
than hiding it. The host-level `free` of a `cplus_malloc` pointer is valid C
(same underlying allocator); only read-only cplus queries are made afterwards.

## Warnings / notes

- Compiler emitted no warnings under `-Wall -Wextra -pedantic -std=c99`.
- `cplus_libc_log_set_enabled(1)` around failure paths prints a few
  `[CPLUS-LIBC] ...: enter` lines to stderr by design; return values are
  unaffected.
- Observed implementation choices documented in the test (not treated as
  bugs): zero-size requests map to 1 backing byte; `elem_size == 0` in
  `cplus_alloc_query_length` means "use the recorded element size".
