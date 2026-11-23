# Additional safeclib / Secure-CRT source integrated into the checked C runtime

**Date:** 2026-11-23
**Branch:** `agent/modern-stdlib`
**Status:** source, header and tests added; build wiring is left to the parent
agent (this change deliberately touches no build script).

## Scope

This note records a second, additive reuse of the already-vendored
`rurban/safeclib` tree. The existing integration (`scripts/build_safecrt.sh`)
compiles 18 upstream `.c` files plus the `timingsafe_*` pair into
`build/safe/libcplus_safecrt.a`. This change ports two further standalone
functions that are **not** in that compile list into the checked C runtime as
`libc/src/string/upstream_safecrt.c`, with a public header and a standalone
C99 test.

## Exact provenance

From `docs/libc/DEPENDENCY_PROVENANCE.md` (row 1) and the on-disk
`third_party/safeclib/{UPSTREAM_COMMIT,PROVENANCE.md,COPYING}`:

| Attribute | Value |
|---|---|
| Upstream | `rurban/safeclib` — <https://github.com/rurban/safeclib> |
| Pinned commit | `90b076e94c4b0fb202dddf948d5d620e5395c1b0` |
| Upstream release/tag | **undetermined locally** (the `VERSION "3.9.1-cplus"` in `scripts/build_safecrt.sh` is a locally generated config value, not an upstream pin) |
| License | MIT — `third_party/safeclib/COPYING`; Copyright (C) 2012, 2013 Cisco Systems; Copyright (C) 2017 Reini Urban |
| Vendored | 2026-11-21, full `src/` and `include/` trees |

Functions integrated (both MIT, both absent from the existing safecrt archive):

| Public symbol | Upstream path at pin | Upstream body | Purpose |
|---|---|---|---|
| `cplus_sc_upstream_bsearch_s` | `third_party/safeclib/src/misc/bsearch_s.c` | `_bsearch_s_chk` | C11 K.3.6.3.1 bounds-checked binary search with a user `context` passed to `compar` |
| `cplus_sc_upstream_memrchr_s` | `third_party/safeclib/src/extmem/memrchr_s.c` | `_memrchr_s_chk` → `_memrchr_s_uchk` | safeclib extension: last occurrence of a byte in a bounded buffer |

The MIT notice of each upstream file is reproduced verbatim in
`libc/src/string/upstream_safecrt.c`. The error numbers and `RSIZE_MAX_MEM`
used by the ports are copied from `safe_lib_errno.h` and the locally generated
`safe_types.h` respectively.

## Adaptations (deliberate, minimal)

1. **Symbol rename / 6- and 4-argument public signatures.** `_bsearch_s_chk`
   lost its trailing `basebos` parameter; `_memrchr_s_chk` lost `destbos`.
   Object-size (`__builtin_object_size`) checks are therefore not part of these
   entry points. The `nmemb/size`/`dmax` vs `RSIZE_MAX_MEM` checks remain.
2. **No safeclib dependency.** `safeclib_private.h` is not included and no
   `libcplus_safecrt.a` symbol is referenced. `invoke_safe_mem_constraint_handler`
   and `invoke_safe_str_constraint_handler` become a file-local no-op; `errno`
   is still set at exactly the same sites as upstream `bsearch_s`.
3. **Portable fallback kept verbatim.** `memrchr_s` uses the upstream
   `#else HAVE_MEMRCHR` loop (`while (dmax--) ...`) byte-for-byte, so no
   platform `memrchr()` is required.
4. **`likely`/`unlikely` and `BND_CHK_PTR_BOUNDS`** reduce to plain C / a
   no-op; the binary-search loop itself is unchanged.
5. **C99-clean header.** `rsize_t` is spelled `size_t` and `errno_t` is spelled
   `int` (identical to the pinned safeclib typedefs), so the header needs only
   `<stddef.h>`. Error constants are re-exported with a `CPLUS_SC_UPSTREAM_`
   prefix to avoid colliding with the host CRT or the existing safecrt headers.

## Files added

- `libc/src/string/upstream_safecrt.c` — two ported functions + verbatim MIT notices + provenance block.
- `libc/install-include/cplus_safecrt_upstream.h` — public declarations and error constants.
- `tests/libc/test_safecrt_upstream.c` — standalone C99 coverage for success, not-found, context threading, NUL-byte search, and every constraint path.
- `docs/stdlib/upstream_reuse/SAFECRT_INTEGRATION.md` — this file.

No build script, compiler, runtime source, `third_party/` file or git index was
modified. This C source does not require strict-mode C+.

## Verification (exact commands and results)

The pre-existing hardened archive at `build/libc/libcplus_hardened.a` is linked
in to confirm the new object coexists with it without symbol collisions. All
new build artifacts go to the session temp directory; no extra repository file
is created.

```sh
TMP=/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode

# 1. Compile the new object directly with the required compiler/standard.
/usr/bin/clang -std=c99 -Wall -Wextra -pedantic \
  -Ilibc/install-include \
  -c libc/src/string/upstream_safecrt.c \
  -o "$TMP/upstream_safecrt.o"

# 2. Build the standalone test against that object + the hardened archive.
/usr/bin/clang -std=c99 -Wall -Wextra -pedantic \
  -Ilibc/install-include \
  tests/libc/test_safecrt_upstream.c \
  "$TMP/upstream_safecrt.o" build/libc/libcplus_hardened.a \
  -o "$TMP/test_safecrt_upstream"

# 3. Run it.
"$TMP/test_safecrt_upstream"

# 4. Check exported symbols.
nm -g "$TMP/upstream_safecrt.o"
```

Result: both compilations clean (no warnings/errors), test prints
`test_safecrt_upstream: all checks passed`, exit status 0. `nm -g` shows
exactly the two new global functions (`cplus_sc_upstream_bsearch_s`,
`cplus_sc_upstream_memrchr_s`) plus the expected `___error` (errno) import;
no safeclib-internal dependencies.

An additional throwaway differential harness in the same temp directory
compared the ports against naive reference scans over 4000 randomized cases
(sorted arrays for `bsearch_s`, arbitrary byte buffers for `memrchr_s`):
`differential: 0 mismatch(es) over 4000 randomized cases`, exit status 0.
The first run of that harness used unsorted arrays and reported mismatches —
expected, because C11 `bsearch_s` requires the array to be partitioned with
respect to the key; sorting the array before the call removed all mismatches.
No repository file was changed by this differential check.

## Parent-agent build wiring (not done here)

Add `libc/src/string/upstream_safecrt.c` to the `for f in ...` list in
`tests/libc/build_libc.sh` so `upstream_safecrt.o` enters
`libcplus_hardened.a`; add `tests/libc/test_safecrt_upstream.c` to the libc
test runner. Because the object is self-contained, no `-D` symbol prefixing or
safeclib include path is needed.
