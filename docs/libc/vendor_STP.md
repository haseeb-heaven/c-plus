# Vendored safeclib `stpcpy_s` / `stpncpy_s` in the checked runtime

## Outcome

The already-vendored rurban/safeclib implementations of `stpcpy_s` and
`stpncpy_s` are now present in the C+ checked runtime as **prefixed, additive**
symbols:

| Exported symbol | Upstream source |
| --- | --- |
| `cplus_v__stpcpy_s_chk` | `third_party/safeclib/src/extstr/stpcpy_s.c` |
| `cplus_v__stpncpy_s_chk` | `third_party/safeclib/src/extstr/stpncpy_s.c` |

No algorithm was rewritten. The two upstream `.c` files are `#include`d
**verbatim** by `libc/src/safecrt_vendor/stp_group.c`; not one line of either
upstream body is copied or edited. The only changes are preprocessor-level
renames (prefix-only). This is an **additive ABI**: no existing symbol is
replaced or removed, and neither function is compiled by
`scripts/build_safecrt.sh` (which builds `strcpy_s`, `strncpy_s`, etc. but not
the `extstr/stpcpy_s.c` / `extstr/stpncpy_s.c` extension group).

## Provenance

- Upstream repository: `rurban/safeclib`
  <https://github.com/rurban/safeclib>
- Pinned commit: `90b076e94c4b0fb202dddf948d5d620e5395c1b0`
  (`third_party/safeclib/UPSTREAM_COMMIT`)
- License: MIT, `third_party/safeclib/COPYING`
  (Copyright (C) 2012, 2013 Cisco Systems; Copyright (C) 2017 Reini Urban)
- Per-file notice: "Copyright (c) 2020 by Reini Urban", reproduced verbatim at
  the top of each included upstream file. The shared COPYING notice is also
  reproduced in the header comment of `stp_group.c`.

## Prefix-only changes

In `libc/src/safecrt_vendor/stp_group.c`:

```c
#define stpcpy_s cplus_v_stpcpy_s
#define _stpcpy_s_chk cplus_v__stpcpy_s_chk
#define stpncpy_s cplus_v_stpncpy_s
#define _stpncpy_s_chk cplus_v__stpncpy_s_chk
#define invoke_safe_str_constraint_handler \
    cplus_sc_invoke_safe_str_constraint_handler
#define _strnlen_s_chk cplus_sc__strnlen_s_chk

#include "extstr/stpcpy_s.c"
#include "extstr/stpncpy_s.c"
```

- Upstream defines `stpcpy_s` / `stpncpy_s` as **function-like macros** in
  `safe_str_lib.h` that expand to `_stpcpy_s_chk` / `_stpncpy_s_chk`. With
  `FOR_DOXYGEN` undefined (the normal build), the upstream files emit only the
  two `_chk` workers. The `cplus_v_stpcpy_s` / `cplus_v_stpncpy_s` defines are
  kept verbatim from the prescribed prefix rule but are overwritten by those
  header macros; `nm -g` confirms that only the two `_chk` symbols exist. The
  public header therefore declares exactly those two real symbols.
- Helper binding (name-only, no new handler or reimplementation):
  `invoke_safe_str_constraint_handler` and `strnlen_s`'s unchecked-name target
  `_strnlen_s_chk` are aliased to the already-linked `cplus_sc_`-prefixed copies
  in `build/safe/libcplus_safecrt.a`. This is required because the archive is
  compiled with `-Dinvoke_safe_str_constraint_handler=cplus_sc_invoke_safe_str_constraint_handler`
  and `-Dstrnlen_s=cplus_sc_strnlen_s`. `handle_str_bos_overflow` needs no alias:
  the archive exports that internal helper with its unprefixed name.
  `-Wno-macro-redefined` is used as prescribed for the macro overwrite above.
- No config macro is needed beyond the generated `build/safe/config.h`;
  `safeclib_private.h` resolves `EXPORT`, `unlikely`, `RCNEGATE`, the BOS macros
  and the error codes from the pinned headers. `build/safe/config.h` does not
  define `SAFECLIB_STR_NULL_SLACK` or the `__builtin_object_size` probes, so the
  upstream error paths use `handle_error()`'s single-NUL termination and trust
  the explicit `destbos`/`srcbos` arguments passed by the caller.

## Files created (all within the assigned ownership)

- `libc/src/safecrt_vendor/stp_group.c` — prefix shim + verbatim includes
- `libc/install-include/cplus_vendor_stp.h` — prefixed declarations and error codes
- `tests/libc/test_vendor_stp.c` — standalone C99 behavioral test
- `docs/libc/vendor_STP.md` — this record

## Exact commands and results

All commands were run from the repository root on branch `agent/modern-stdlib`.

1. Ensure the generated config/archive (the build script defaults to `clang`
   from PATH; in this environment that is a Homebrew LLVM which cannot find
   `MacOSX27.sdk`, so `CPLUS_CLANG=/usr/bin/clang` is used — this does not
   change any vendored source):

   ```sh
   CPLUS_CLANG=/usr/bin/clang sh scripts/build_safecrt.sh >/dev/null 2>&1
   # -> exit 0; build/safe/config.h and build/safe/libcplus_safecrt.a present
   ```

2. Compile the verbatim-inclusion TU with the prescribed command:

   ```sh
   /usr/bin/clang -O2 -std=c99 -D__STDC_WANT_LIB_EXT1__=0 \
     -Ithird_party/safeclib/include -Ibuild/safe \
     -Ithird_party/safeclib/src -Istdlib/safe -Wno-macro-redefined \
     -c libc/src/safecrt_vendor/stp_group.c -o /tmp/stp_group.o
   ```

   Result: exit 0, clean compile (no warnings/errors).

3. Confirm symbols:

   ```sh
   nm -g /tmp/stp_group.o
   ```

   ```
                    U _cplus_sc__strnlen_s_chk
                    U _cplus_sc_invoke_safe_str_constraint_handler
   0000000000000000 T _cplus_v__stpcpy_s_chk
   00000000000001fc T _cplus_v__stpncpy_s_chk
                    U _handle_str_bos_overflow
   ```

   Only `cplus_v__stpcpy_s_chk` and `cplus_v__stpncpy_s_chk` are defined; the
   three undefined references are resolved by `libcplus_safecrt.a`.

4. Link and run the standalone test under a 30-second cap:

   ```sh
   /usr/bin/clang -O2 -std=c99 -Ilibc/install-include \
     tests/libc/test_vendor_stp.c /tmp/stp_group.o \
     build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
     -o /tmp/test_vendor_stp
   timeout 30 /tmp/test_vendor_stp
   ```

   Result: clean link (no duplicate symbols), exit status 0. Complete output:

   ```
   vendor_stp: 30 0
   ```

   The test registers a no-op constraint handler from `libcplus_safecrt.a`
   (`cplus_sc_set_str_constraint_handler_s`) so only the required summary line
   is emitted; the deliberate constraint violations still return their
   upstream error codes. 30 checks, 0 failures.

## Test coverage

`test_vendor_stp.c` is standalone C99 and declares only the two symbols present
in `/tmp/stp_group.o`. It covers:

- Valid copies for both functions, including the **pointer-to-terminator**
  return value (`dest + strlen(src)`) and `*errp == EOK`.
- `stpncpy_s` truncation by `slen`: copies `slen` characters, appends NUL,
  returns `dest + slen`.
- NULL `errp`, NULL `dest`, NULL `src` for both functions (ESNULLP / NULL
  return, and dest nulled on the NULL-src path).
- Zero bounds: `dmax == 0` (ESZEROL) and `stpncpy_s` `slen == 0` (empty string).
- One-over bounds: `dmax == sizeof(dest) + 1` with the real object size passed
  as `destbos`, yielding `EOVERFLOW` and a nulled dest.
- Truncation by `dmax`: source longer than the destination, yielding ESNOSPC
  and a nulled dest.
- Over-max lengths with unknown object size: `dmax`/`slen` greater than
  `RSIZE_MAX_STR`, yielding ESLEMAX.

The test prints `vendor_stp: <checks> <failures>` and exits nonzero on any
failure.
