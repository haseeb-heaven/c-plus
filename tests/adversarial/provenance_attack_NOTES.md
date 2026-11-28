# Provenance attack — adversarial notes

Test: `tests/libc/test_provenance_attack.c`
Build/link/verify: see the task's VERIFY block; also confirmed with
`-fsanitize=undefined -fno-sanitize-recover=all`.

## Result

No crash, no false "live"/"valid" answer, no cross-free. UBSan clean. The test
prints only `provenance_attack: ok` (the stray `[CPLUS-LIBC] ...` lines are the
libc log, expected when logging is enabled).

## Attacks run and what held

1. **Cross-allocator metadata isolation** — `cplus_malloc` pointer is the only
   one reported live; a host `malloc` pointer and a `cplus_aligned_alloc`
   (host-aligned) pointer both report `is_live == 0`, `size == 0`, and
   `get_meta` fails. Each freed with its correct allocator. Held.
2. **Wrong-table double-free detection** — `cplus_free_checked` refuses a
   previously `cplus_free`d pointer, a host `malloc` pointer, and clears
   `*inout`; NULL returns defined 0. No crash; the refused host pointer was
   proven intact by writing/reading it before its correct `free`. Held.
3. **Generation forgery** — `gen+1` and `0` rejected; old generation rejected
   after free. Held.
4. **Ownership transfer** — src NULLed, dst is the original pointer, block
   stays live, generation bumped; `dst==src`, NULL dst, NULL src, non-empty
   dst, and already-transferred (NULL) src all refused with defined nonzero;
   exactly one owner freed. Held.
5. **Size-query forgeries** — interior (`ptr+1`), host, and freed pointers all
   return size 0 / `query_length` EINVAL with zeroed out-count / failed
   `get_meta`. Held.
6. **Checked allocator isolation** — `malloc_s` pointer is live in the SafeCRT
   table but NOT the cplus table; a cplus pointer is NOT live in the SafeCRT
   table; `free_checked_s` on a foreign cplus pointer returns EINVAL and leaves
   it untouched (then freed with `cplus_free`). Held.
7. **Logging on/off** — failure-path return-value bitmask identical with
   logging disabled/enabled. Held.
8. **Extra stress** — 256 live blocks forced metadata table growth, then mass
   free and mass address reuse: no false-live, no corrupted size, no forged
   generation accepted on a reused address. Held.

## Non-breaking observation (NOTE, not a FAILING)

When `cplus_free_checked` is given a **host `malloc` pointer** whose address
coincides with a recently freed cplus block, the diagnostic can be attributed
to the **double-free** counter instead of the **invalid-free** counter (the
bounded `g_recent` cache is keyed by hashed address and a host pointer can
legitimately reuse a just-freed cplus address, since cplus uses the host
allocator underneath). This is harmless: the free is still **refused** with
`EINVAL`, the caller's pointer is untouched, no cross-free occurs, and the
process does not crash — the only difference is which internal diagnostic
counter increments. This is consistent with the recent-cache limits already
documented in `stdlib/safe/safe_alloc.h` / `libc/src/stdlib/alloc.c`.

Because of this, the test asserts the robust invariant
`double_free_count + invalid_free_count` increases (a foreign pointer must trip
exactly one diagnostic) rather than over-specifying which counter must fire.
This is a diagnostic-fidelity nuance, **not** a provenance/ownership break.
