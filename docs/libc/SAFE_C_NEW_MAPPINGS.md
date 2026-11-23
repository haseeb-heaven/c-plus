# New ordinary-name `_s` macro mappings in `safe_c.h`

Branch `agent/modern-stdlib`. This record covers the additive mapping work in
`stdlib/safe/safe_c.h`, its regression test
`tests/crt/unit/test_safe_c_mapping.c`, and the exact local verification run.

## Constraint honoured

Only macro definitions were added. No compiler, runtime, archive, exported C
symbol, or struct layout was changed. Every new mapping resolves to a function
that is **already compiled** into one of the two shipped archives, verified
with `nm`. No fake declaration and no new wrapper function was introduced.

## 1. Symbol audit (`nm` on the existing archives)

Commands run from the repository root, verbatim:

```sh
nm -g --defined-only build/safe/libcplus_safecrt.a \
  | grep -Ei 'memchr_s|memrchr_s|bsearch_s|qsort_s|strtok_s|strerror_s' | sort -u
nm -g --defined-only build/libc/libcplus_hardened.a \
  | grep -Ei 'memchr_s|memrchr_s|bsearch_s|qsort_s|strtok_s|strerror_s' | sort -u
nm build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
  | grep -Ei 'memchr_s|qsort_s'
```

Results (first archive):

```
0000000000000000 T _cplus_sc__strerror_s_chk
0000000000000000 T _cplus_sc__strtok_s_chk
0000000000000070 T _cplus_sc_strerror_s
0000000000000078 T _cplus_sc_strtok_s
00000000000000b8 T __strerror_s_uchk
000000000000053c T _cplus_strtok_s
00000000000005f4 T _cplus_strerror_s
sc_strerror_s.o:
sc_strtok_s.o:
```

Results (second archive):

```
0000000000000000 T _cplus_sc_upstream_bsearch_s
00000000000000d4 T _cplus_sc_upstream_memrchr_s
```

The third command (which also searches undefined/local symbols) produced **no
output**: neither `memchr_s` nor `qsort_s` exists as a symbol in either archive.

### Decision table

| `_s` name | Compiled target | Archive | Action |
|---|---|---|---|
| `memchr_s` | none (all nm variants empty) | — | **Not mapped.** Adding a macro would be a fake declaration. |
| `memrchr_s` | `cplus_sc_upstream_memrchr_s` | `libcplus_hardened.a` | **Added** in `safe_c.h`. |
| `bsearch_s` | `cplus_sc_upstream_bsearch_s` | `libcplus_hardened.a` | **Added** in `safe_c.h`. |
| `qsort_s` | none (all nm variants empty) | — | **Not mapped.** No compiled implementation to target. |
| `strtok_s` | `cplus_strtok_s` | `libcplus_safecrt.a` | Already mapped in `safe_string.h`; left unchanged and now covered by the new test. |
| `strerror_s` | `cplus_strerror_s` | `libcplus_safecrt.a` | Already mapped in `safe_string.h`; left unchanged and now covered by the new test. |

The two compiled targets are declared in
`libc/install-include/cplus_safecrt_upstream.h` (vendored safeclib
`src/extmem/memrchr_s.c` and `src/misc/bsearch_s.c`, pinned commit recorded in
that header). That header is now included by `safe_c.h`; it lives in the same
`libc/install-include` directory as the `cplus_libc.h` include that `safe_c.h`
already required, so no new include path is imposed on users.

## 2. Added mappings

### `memrchr_s` — two forms, variadic macro

```c
memrchr_s(dest, ch, resultp)              /* 3 args */
memrchr_s(dest, dmax, ch, resultp)        /* 4 args */
```

- Compiled target: `int cplus_sc_upstream_memrchr_s(const void *dest,
  size_t dmax, int ch, void **resultp)`.
- 3-arg form passes `CPLUS_SAFE_OBJ_SIZE(dest)` (i.e.
  `__builtin_object_size(dest, 1)` where available, `(size_t)-1` otherwise)
  as `dmax`, so a stack/global object is searched to its exact size.
- 4-arg form passes the caller's `dmax` unchanged, but the macro refuses it
  with **ESNOSPC (406)** and sets `*resultp = NULL` when `dmax` exceeds the
  proven object size. This restores the `CHK_DEST_MEM_OVR` check that the
  standalone upstream port omitted; it matches upstream `_memrchr_s_chk`.
- Valid-input semantics are preserved: for `dmax <= BOS(dest)` the compiled
  implementation receives exactly the caller's arguments, and the result
  equals the ordinary `cplus_memrchr(dest, ch, dmax)` pointer.

Error codes (safeclib values, **not** platform `errno`):

| Code | Meaning |
|---|---|
| 0 (`ESOK`) | match found; `*resultp` holds it |
| 400 (`ESNULLP`) | `dest` or `resultp` is NULL |
| 401 (`ESZEROL`) | `dmax == 0` |
| 403 (`ESLEMAX`) | `dmax > RSIZE_MAX_MEM` (16384) or `ch > 255` |
| 406 (`ESNOSPC`) | caller `dmax` exceeds `__builtin_object_size(dest)` (macro guard) |
| 409 (`ESNOTFND`) | no match in the first `dmax` bytes |

When `BOS(dest)` is unknown, the 3-arg form passes `(size_t)-1` and the
compiled implementation reports `ESLEMAX (403)`; use the explicit 4-arg form
with a capacity derived from tracked allocation metadata in that case.

### `bsearch_s` — single 6-argument mapping

```c
bsearch_s(key, base, nmemb, size, compar, context)
```

- Compiled target: `void *cplus_sc_upstream_bsearch_s(const void *key,
  const void *base, size_t nmemb, size_t size,
  int (*compar)(const void *, const void *, void *), void *context)`.
- The signature is unchanged, so `context = NULL` gives the ordinary
  `bsearch` call shape with a context-aware comparator.
- The macro adds the BOS check the standalone port omitted: if
  `nmemb * size > __builtin_object_size(base)` it returns `NULL` and sets
  `errno = ESNOSPC (406)` (overflow-safe division form; `nmemb == 0` skips
  the check). Without this guard the compiled implementation walks past the
  array — confirmed by the pre-change probe (`nmemb = 7` on a 6-element array
  returned a bogus match with `errno = 0`).
- Valid-input semantics are preserved: for `nmemb * size <= BOS(base)` the
  compiled implementation receives the caller's arguments unchanged and
  returns the same element as ordinary `bsearch`.

Error reporting:

| `errno` value | Meaning |
|---|---|
| 0 (`ESOK`) | clean search (found or not found) |
| 400 (`ESNULLP`) | `key`, `base` or `compar` is NULL while `nmemb != 0` |
| 403 (`ESLEMAX`) | `nmemb` or `size` exceeds `RSIZE_MAX_MEM` (16384) |
| 406 (`ESNOSPC`) | `nmemb * size` exceeds `BOS(base)` (macro guard) |

### Already-present mappings kept as-is

- `strtok_s` → `cplus_strtok_s(dest, dmax, delim, ptr)`. Returns the next token
  or NULL; sets `errno` to 400 (`ESNULLP`) for NULL `dest`/`delim`/`ptr` or
  401 (`ESZEROL`) for `*dmax == 0`.
- `strerror_s` → `cplus_strerror_s(dest, dmax, errnum)`. Returns 0 on success;
  400 (`ESNULLP`), 401 (`ESZEROL`), 402 (`ESLEMIN`, destination too small),
  403 (`ESLEMAX`). Messages longer than `dmax - 1` are truncated with `...`
  when `dmax > 3`.

## 3. Tests

`tests/crt/unit/test_safe_c_mapping.c` covers, for the newly mapped names:

- valid-input equivalence with the ordinary spelling (`cplus_memrchr` and
  system `bsearch`),
- NULL argument, zero length/count, exact-fit, and one-over cases,
- clean not-found and `ch > 255` for `memrchr_s`,
- NULL key/base/comparator for `bsearch_s`,

and keeps the pre-existing `strtok_s`/`strerror_s` mappings covered with valid
input plus NULL/zero/exact-fit/too-small cases. It declares no functions of
its own; all `_s` entry points come from the shipped headers.

## 4. Verification (exact commands and results)

Compiler: `Apple clang version 16.0.0 (clang-1600.0.26.6)`, invoked as
`/usr/bin/clang` from the repository root.

```sh
/usr/bin/clang -O2 -Wall -Wextra -pedantic -std=c99 \
  -I. -Istdlib/safe -Ilibc -Ilibc/install-include \
  -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include \
  tests/crt/unit/test_safe_c_mapping.c \
  build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
  -lm -pthread \
  -o build/libc/test_safe_c_mapping
build/libc/test_safe_c_mapping
```

Compile result: exit 0, **zero warnings**. Run result: exit 0, output:

```
IGNORE CONSTRAINT HANDLER: (400) strtok_s: dest/*ptr is null
IGNORE CONSTRAINT HANDLER: (401) strtok_s: *dmaxp is 0
IGNORE CONSTRAINT HANDLER: (400) strerror_s: dest is null
IGNORE CONSTRAINT HANDLER: (401) strerror_s: dmax is 0
IGNORE CONSTRAINT HANDLER: (402) strerror_s: dmax is too small
safe_c_mapping: ok
```

The `IGNORE CONSTRAINT HANDLER` lines are the safeclib IGNORE-mode handler
reporting the deliberately-triggered constraint cases; they are expected.

Header-regression check (same flags, existing consumers):

```sh
/usr/bin/clang -O2 -Wall -Wextra -pedantic -std=c99 \
  -I. -Istdlib/safe -Ilibc -Ilibc/install-include \
  -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include \
  tests/crt/unit/test_safe_c_api.c \
  build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
  -lm -pthread -o build/libc/test_safe_c_api_mappingcheck
/usr/bin/clang -O2 -Wall -Wextra -pedantic -std=c99 \
  -I. -Istdlib/safe -Ilibc -Ilibc/install-include \
  -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include \
  tests/crt/unit/test_auto_bounds_s.c \
  build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
  -lm -pthread -o build/libc/test_auto_bounds_s_mappingcheck
```

Both compiled and ran successfully (each printed its own `ok` line to stdout,
suppressed during the check).

Archive hashes before and after the header/test/doc edits (unchanged — no
compiled artifact was touched):

```
58541c705827a83ac3489bee9ce692d3e3810b07  build/safe/libcplus_safecrt.a
7310f18420baf6ef95afeebe977b5fcf15187871  build/libc/libcplus_hardened.a
```

## 5. Honest limits

- `memchr_s` and `qsort_s` are intentionally **absent**. The pinned safeclib
  tree has source for them, but neither is compiled into the shipped archives;
  adding a macro now would violate the "backed by a compiled implementation"
  rule. Integration would require an archive rebuild by the archive owner.
- The BOS guards only fire when `__builtin_object_size` can prove the object
  size. For heap or otherwise opaque pointers the guard is inactive and the
  compiled implementation's own constraints apply.
- Macro arguments should be side-effect free: the BOS operand must name a
  compile-time-known object.
- These are runtime constraint checks, not a proof of pointer provenance,
  lifetime, or alias safety. See `docs/libc/C_BUG_CLASS_MATRIX.md`.
- No live-game or injected-DLL behaviour is claimed; this is header + archive
  verification only.
