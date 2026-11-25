# Vendored safeclib `strlcpy` / `strlcat` in the checked runtime

## Outcome

The already-vendored rurban/safeclib implementations of `strlcpy` and
`strlcat` are now present in the C+ checked runtime as **prefixed, additive**
symbols:

| Exported symbol | Upstream source |
| --- | --- |
| `cplus_v_strlcpy` | `third_party/safeclib/src/extstr/strlcpy.c` |
| `cplus_v_strlcat` | `third_party/safeclib/src/extstr/strlcat.c` |

No algorithm was rewritten. The two upstream `.c` files are `#include`d
**verbatim** by `libc/src/safecrt_vendor/strl_group.c`. The only changes are
preprocessor-level renames (prefix-only) and a header preload to control the
SDK's own fortify macros; neither touches a line of the upstream bodies.
This is an **additive ABI**: no existing symbol is replaced or removed.

## Provenance

- Upstream repository: `rurban/safeclib`
- Pinned commit: `90b076e94c4b0fb202dddf948d5d620e5395c1b0`
- License: MIT, `third_party/safeclib/COPYING`
  (Copyright (C) 2012, 2013 Cisco Systems; Copyright (C) 2017 Reini Urban)
- Per-file copyright notices (Copyright (c) 2026 by Reini Urban) are
  reproduced verbatim inside the included upstream sources; the COPYING
  notice is also reproduced at the top of `strl_group.c`.

## Prefix-only changes

In `libc/src/safecrt_vendor/strl_group.c`:

```c
#define strlcat cplus_v_strlcat
#define _strlcat_chk cplus_v__strlcat_chk
#define strlcpy cplus_v_strlcpy
#define _strlcpy_chk cplus_v__strlcpy_chk
#include "extstr/strlcat.c"
#include "extstr/strlcpy.c"
```

- `cplus_v__strlcat_chk` / `cplus_v__strlcpy_chk`: the `_chk` macros are
  defined for symmetry with the task's prefix rule, but **upstream exports no
  `_chk` variant** for these two functions — `nm -g` confirms no such symbol
  exists. The public header therefore declares only the two real functions.
- Helper binding (prefix-only, no new handler): `invoke_safe_str_constraint_handler`
  is aliased to `cplus_sc_invoke_safe_str_constraint_handler`, the already-checked
  symbol that `scripts/build_safecrt.sh` exports from
  `build/safe/libcplus_safecrt.a`. This is required because the archive is
  compiled with
  `-Dinvoke_safe_str_constraint_handler=cplus_sc_invoke_safe_str_constraint_handler`.
  The default handler is `ignore_handler_s`, so NULL-constraint calls are
  non-fatal.
- Platform fortify adaptation: on macOS, `<string.h>` defines `strlcat`/`strlcpy`
  as FORTIFY macros in `<secure/_string.h>`. `safeclib_private.h` (and thus the
  SDK headers) is included first, then the platform macros are `#undef`'d, then
  the `cplus_v_*` prefixes are defined. The upstream `.c` files include
  `safeclib_private.h` again but it is include-guarded, so the prefixes win at
  the verbatim definition sites. `-Wno-macro-redefined` is used as prescribed.
- No config macro is needed beyond the generated `build/safe/config.h`;
  `safeclib_private.h` resolves `EXPORT`, `unlikely`, `ESNULLP` and the
  constraint-handler declaration from the pinned headers.

## Files created (all within the assigned ownership)

- `libc/src/safecrt_vendor/strl_group.c` — prefix shim + verbatim includes
- `libc/install-include/cplus_vendor_strl.h` — prefixed declarations
- `tests/libc/test_vendor_strl.c` — standalone C99 oracle/parity test
- `docs/libc/vendor_STRL.md` — this record

## Exact commands and results

All commands were run from the repository root on branch `agent/modern-stdlib`.

1. Ensure the generated config (the build script defaults to `clang` from
   PATH; in this environment that is a Homebrew LLVM which cannot find the
   SDK, so `CPLUS_CLANG=/usr/bin/clang` is used — this does not change any
   vendored source):

   ```sh
   CPLUS_CLANG=/usr/bin/clang sh scripts/build_safecrt.sh >/dev/null 2>&1
   # -> OK; build/safe/config.h and build/safe/libcplus_safecrt.a present
   ```

   (Plain `sh scripts/build_safecrt.sh` fails in this container because PATH
   `clang` selects a non-existent `MacOSX27.sdk`; with `/usr/bin/clang` it
   succeeds.)

2. Compile the verbatim-inclusion TU:

   ```sh
   /usr/bin/clang -O2 -std=c99 -D__STDC_WANT_LIB_EXT1__=0 \
     -Ithird_party/safeclib/include -Ibuild/safe \
     -Ithird_party/safeclib/src -Istdlib/safe -Wno-macro-redefined \
     -c libc/src/safecrt_vendor/strl_group.c -o /tmp/strl_group.o
   ```

   Result: clean compile, no warnings/errors.

3. Confirm symbols:

   ```sh
   nm -g /tmp/strl_group.o
   ```

   ```
                    U _cplus_sc_invoke_safe_str_constraint_handler
   0000000000000000 T _cplus_v_strlcat
   00000000000000ec T _cplus_v_strlcpy
                    U _strlen
   ```

   Only `cplus_v_strlcat` and `cplus_v_strlcpy` are defined; the two undefined
   symbols are resolved by `libcplus_safecrt.a` and the system libc,
   respectively.

4. Link and run the standalone test under a 30-second cap:

   ```sh
   /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -Ilibc/install-include \
     tests/libc/test_vendor_strl.c /tmp/strl_group.o \
     build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
     -o /tmp/test_vendor_strl
   timeout 30 /tmp/test_vendor_strl
   ```

   Result: clean link (no duplicate symbols), exit status 0. Output:

   ```
   IGNORE CONSTRAINT HANDLER: (400) strlcpy: dest is null
   IGNORE CONSTRAINT HANDLER: (400) strlcpy: src is null
   IGNORE CONSTRAINT HANDLER: (400) strlcpy: src is null
   IGNORE CONSTRAINT HANDLER: (400) strlcat: dest is null
   IGNORE CONSTRAINT HANDLER: (400) strlcat: src is null
   vendor_strl: 2070 0
   ```

   The `IGNORE CONSTRAINT HANDLER` lines are the safeclib default
   (`ignore_handler_s`) reporting the deliberate NULL-constraint cases; they
   are expected and non-fatal. `2070` checks, `0` failures.

## Test coverage

`test_vendor_strl.c` is standalone C99 and uses a local, independently written
reference implementation of the documented BSD/safeclib semantics as an
oracle (no host `strlcpy`/`strlcat` is used). It covers:

- `strlcpy`: truncation, exact fit, `dsize == 0`, every `dsize` from 0 to 40
  crossed with five source strings — return value and full destination
  buffer compared against the oracle.
- `strlcat`: the same sweep crossed with four initial destination strings
  (precondition established), plus the "no NUL within `dsize`" path where
  upstream returns `dsize + strlen(src)` without writing.
- NULL constraint cases for both functions (NULL dest, NULL src with and
  without a non-zero `dsize`), verifying upstream's documented returns.
- Result line `vendor_strl: <checks> <failures>` and a non-zero exit when any
  check fails.
