# Hardened allocation wrappers (`alloc_hardened`)

Additive failure-atomic allocation APIs layered on the existing C+ hardened
libc allocator. Goal: make allocation failures **non-corrupting** and
**observable** without changing pointer representation, the metadata side
table, or any existing symbol.

## Added files (only these)

| File | Purpose |
| --- | --- |
| `libc/src/stdlib/alloc_hardened.c` | Three new `cplus_alloc_*` wrappers |
| `libc/install-include/cplus_alloc_hardened.h` | Public declarations / contract |
| `tests/libc/test_alloc_hardened.c` | Behavioural test, prints `alloc_hardened: <checks> <failures>` |
| `docs/libc/ALLOC_HARDENED.md` | This record |

`libc/src/stdlib/alloc.c` was read only. No `runtime/**` or `compiler/**` file
was touched. Nothing was staged or committed.

## API

```c
int  cplus_alloc_atomic_realloc(void **ptr, size_t new_size);
int  cplus_alloc_checked_free(void **ptr);
void cplus_alloc_state_dump(FILE *stream);
```

* **`cplus_alloc_atomic_realloc`** returns `0` on success, `ENOMEM` on an
  injected or real allocation failure, `EINVAL` for a NULL slot or an
  untracked/freed pointer. On *any* non-zero return, `*ptr` and every byte of
  the original block are unchanged. Before calling `cplus_realloc` it
  snapshots `cplus_alloc_get_meta`; after a failure it re-reads the metadata
  and requires the same live flag, `byte_size`, and `generation`. The new
  address is published to `*ptr` only after the underlying realloc succeeded.
  A NULL `*ptr` follows realloc semantics and performs a fresh allocation.
* **`cplus_alloc_checked_free`** returns `0` and sets `*ptr = NULL` only after
  a real free. A NULL slot, a NULL `*ptr`, or a pointer that
  `cplus_alloc_is_live` reports as not live (double free / stale / foreign)
  returns `EINVAL` and leaves the caller's slot untouched. The underlying
  `cplus_free_checked` is called through a local copy, so a refused free can
  never NULL the caller's pointer.
* **`cplus_alloc_state_dump`** writes exactly 12 fixed lines: an aggregate
  live-allocation view from `cplus_alloc_live_count`,
  `cplus_alloc_live_bytes`, `cplus_alloc_double_free_count`, and
  `cplus_alloc_invalid_free_count`, plus wrapper counters
  (`atomic_realloc_ok/fail/invalid/inconsistent`, `checked_free_ok/refused`,
  `state_dumps`). It is bounded, prints no pointer values and no paths, and a
  NULL stream is ignored.

All three wrappers are new names with the `cplus_alloc_*` prefix. They call
only existing public metadata APIs; there is no new allocator internal, no
new global state in `alloc.c`, and no change to pointer layout.

## Symbol survey (pre-existing APIs, via `nm`)

Run from the repository root. Underscore prefixes are the Mach-O symbol
rendering of the C names.

```sh
nm -g --defined-only build/libc/libcplus_hardened.a \
  | grep -E ' _cplus_(alloc_fail_after|alloc_fail_reset|alloc_is_live|alloc_size|alloc_get_meta|free_checked|realloc)$' \
  | sort -u
```

```
0000000000000554 T _cplus_realloc
00000000000008c0 T _cplus_free_checked
0000000000000bd8 T _cplus_alloc_get_meta
0000000000000ce8 T _cplus_alloc_is_live
0000000000000da8 T _cplus_alloc_size
00000000000013fc T _cplus_alloc_fail_after
000000000000143c T _cplus_alloc_fail_reset
```

```sh
nm -g --defined-only build/safe/libcplus_safecrt.a \
  | grep -E ' _cplus_safe_(ptr_is_live|ptr_size|ptr_was_freed|alloc_fail_after|alloc_fail_reset)$' \
  | sort -u
```

```
0000000000000de8 T _cplus_safe_ptr_is_live
0000000000000e90 T _cplus_safe_ptr_was_freed
0000000000000f60 T _cplus_safe_ptr_size
00000000000014c8 T _cplus_safe_alloc_fail_after
0000000000001504 T _cplus_safe_alloc_fail_reset
```

Note: `cplus_safe_ptr_was_freed` lives in `libcplus_safecrt.a`, not in
`libcplus_hardened.a`. The hardened side table has no public "was freed" call,
so checked-free refusal is decided by `cplus_alloc_is_live` (a pointer that is
not live is refused), and injected failures come from
`cplus_alloc_fail_after(0)` / `cplus_alloc_fail_reset()`.

## Exact verification commands and results

All commands were run from the repository root with `/usr/bin/clang`
(Apple clang version 16.0.0, target arm64-apple-darwin). The build directory
already contains `build/libc/libcplus_hardened.a`; no rebuild was performed.

```sh
WORK=$(mktemp -d)

/usr/bin/clang -std=c99 -Wall -Wextra -pedantic -Ilibc/install-include \
  libc/src/stdlib/alloc_hardened.c tests/libc/test_alloc_hardened.c \
  build/libc/libcplus_hardened.a -lm -pthread \
  -o "$WORK/test_alloc_hardened"
```

Result: compile succeeded with **no warnings or errors**.

```sh
timeout 30 "$WORK/test_alloc_hardened"
echo "EXIT=$?"
```

```
alloc_hardened: 32 0
EXIT=0
```

32 checks, 0 failures, well within the 30 s timeout.

### Test coverage mapping

| Requirement | Checks in `test_alloc_hardened.c` |
| --- | --- |
| Normal path | fresh allocation, grow, tracked size 64/128, canary after grow |
| Injected failure atomicity | `cplus_alloc_fail_after(0)`, `ENOMEM`, pointer unchanged, `byte_size` and `generation` unchanged, still live |
| Original contents intact | `0xA5` canary at offsets 0 and 127 after injected failure |
| Double-free refusal | second `cplus_alloc_checked_free` on alias returns `EINVAL`, alias not NULLed |
| NULL pointer | NULL slot and NULL value for both wrappers; untracked stack pointer refused; NULL stream tolerated |
| State dump to tmpfile | `tmpfile()`, flush, rewind, read back 8..24 bounded lines, required counter fields present, no `/` character |
| Canary intact | before and after injected failure, and after grow |

### nm evidence — only new symbols

```sh
/usr/bin/clang -std=c99 -Wall -Wextra -pedantic -Ilibc/install-include \
  -c libc/src/stdlib/alloc_hardened.c -o "$WORK/alloc_hardened.o"
nm -g --defined-only "$WORK/alloc_hardened.o"
```

```
0000000000000000 T _cplus_alloc_atomic_realloc
00000000000002a0 T _cplus_alloc_checked_free
00000000000003c4 T _cplus_alloc_state_dump
```

For comparison, the baseline library defines none of them:

```sh
nm -g --defined-only build/libc/libcplus_hardened.a \
  | grep -E 'cplus_alloc_(atomic_realloc|checked_free|state_dump)'
```

```
(none - all three are new)
```

The object's undefined symbols are all pre-existing APIs or libc services:

```sh
nm -g -u "$WORK/alloc_hardened.o" | sort
```

```
___error
_cplus_alloc_double_free_count
_cplus_alloc_get_meta
_cplus_alloc_invalid_free_count
_cplus_alloc_is_live
_cplus_alloc_live_bytes
_cplus_alloc_live_count
_cplus_free_checked
_cplus_realloc
_fprintf
_fputs
_pthread_mutex_lock
_pthread_mutex_unlock
```

The linked test binary exports the same three new globals:

```sh
nm -g --defined-only "$WORK/test_alloc_hardened" \
  | grep -E 'cplus_alloc_(atomic_realloc|checked_free|state_dump)'
```

```
000000010000098c T _cplus_alloc_atomic_realloc
0000000100000c2c T _cplus_alloc_checked_free
0000000100000d50 T _cplus_alloc_state_dump
```

## Scope / limitations

* These are additive wrappers, not a replacement allocator. Existing
  `cplus_realloc`/`cplus_free` call sites keep their current behaviour.
* The check-then-act sequence in `cplus_alloc_checked_free` is metadata-based
  but not atomic against a *different* thread freeing the same `void **`
  slot; the slot must have a single owner. Internal table consistency is
  still protected by the allocator's own lock.
* The state dump is an aggregate, fixed-width diagnostic view by design: it
  cannot enumerate individual live blocks because `alloc.c` intentionally
  exposes no iterator, and bounded output avoids leaking addresses/paths.
