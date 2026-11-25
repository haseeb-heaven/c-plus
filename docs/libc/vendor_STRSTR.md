# Vendored safeclib `strstr_s` / `strcasestr_s` into the checked runtime

## Provenance

- Upstream repository: `rurban/safeclib` <https://github.com/rurban/safeclib>
- Pinned commit: `90b076e94c4b0fb202dddf948d5d620e5395c1b0`
- License: MIT (`third_party/safeclib/COPYING`)
- Upstream sources:
  - `third_party/safeclib/src/extstr/strstr_s.c`
  - `third_party/safeclib/src/extstr/strcasestr_s.c`

Both upstream MIT copyright notices (strstr_s.c: Bo Berry, Nov 2008 / Reini
Urban, Sep 2018, Copyright (c) 2008-2011 Cisco Systems, Inc and (c) 2018 Reini
Urban; strcasestr_s.c: Bo Berry, Nov 2008, Copyright (c) 2008-2011 Cisco
Systems, Inc) are reproduced verbatim in `libc/src/safecrt_vendor/strstr_group.c`.

## Verbatim inclusion — prefix-only changes

`libc/src/safecrt_vendor/strstr_group.c` does **not** reimplement either
function. It `#include`s the two upstream translation units verbatim:

```c
#define strstr_s cplus_v_strstr_s
#define _strstr_s_chk cplus_v__strstr_s_chk
#define strcasestr_s cplus_v_strcasestr_s
#define _strcasestr_s_chk cplus_v__strcasestr_s_chk

#include "extstr/strstr_s.c"
#include "extstr/strcasestr_s.c"
```

The only changes to the included sources are the four prefix-only `#define`s
above. No upstream source line is altered; algorithms, constraint ordering and
return codes are exactly those of the pinned commit.

### Extra config macro (required)

One extra prefix macro is needed because this translation unit is compiled
outside `scripts/build_safecrt.sh`:

```c
#define invoke_safe_str_constraint_handler \
        cplus_sc_invoke_safe_str_constraint_handler
```

**Why:** both upstream bodies call safeclib's constraint handler on a
runtime-constraint violation. `scripts/build_safecrt.sh` compiles safeclib with
`-Dinvoke_safe_str_constraint_handler=cplus_sc_invoke_safe_str_constraint_handler`,
so the archive `build/safe/libcplus_safecrt.a` exports the handler only under
the prefixed name. Without the mapping above, `/tmp/strstr_group.o` has an
unresolved `_invoke_safe_str_constraint_handler` and the test fails to link.
The mapping is itself prefix-only (same symbol, same signature) and resolves
the reference at link time. The handler's default action is `ignore_handler_s`
(a no-op), so constraint violations simply return the documented error codes.

No other config macro was added. `config.h` from `build/safe/config.h` supplies
`STDC_HEADERS` (hence `<ctype.h>`, needed for `toupper` in `strcasestr_s.c`),
`HAVE_STRING_H` (hence `strlen`) and the other feature macros. `__STDC_WANT_LIB_EXT1__`
is set to `0` on the command line as specified.

## Files

| File | Role |
| --- | --- |
| `libc/src/safecrt_vendor/strstr_group.c` | Verbatim upstream inclusion with four prefix defines + one handler prefix mapping |
| `libc/install-include/cplus_vendor_strstr.h` | Self-contained C99 declarations of the prefixed entry points |
| `tests/libc/test_vendor_strstr.c` | Standalone C99 test (found/not-found, NULL, zero/one-over bounds, case-insensitive, valid-input parity) |
| `docs/libc/vendor_STRSTR.md` | This record |

## Symbols

`nm -g /tmp/strstr_group.o` (Darwin prefixes C symbols with `_`):

```
                 U ___toupper
0000000000000434 T __strcasestr_s_uchk
000000000000020c T __strstr_s_uchk
                 U _cplus_sc_invoke_safe_str_constraint_handler
00000000000002fc T _cplus_v__strcasestr_s_chk
0000000000000000 T _cplus_v__strstr_s_chk
                 U _strlen
```

Prefixed exported functions (declared in `cplus_vendor_strstr.h`):

- `cplus_v__strstr_s_chk`
- `cplus_v__strcasestr_s_chk`

Also emitted by the verbatim upstream bodies, unprefixed (internal `_uchk`
helpers, present in the object but not declared by the public header):
`_strstr_s_uchk`, `_strcasestr_s_uchk`. These are additive and do not collide
with any symbol in `libcplus_safecrt.a` or `libcplus_hardened.a`.

The unprefixed `strstr_s` and `strcasestr_s` names exist only in the upstream
`#ifdef FOR_DOXYGEN` declarations and are never compiled, so no `cplus_v_strstr_s`
or `cplus_v_strcasestr_s` function symbol exists. The header deliberately
declares only the two real `_chk` entry points.

Undefined references (`__toupper`, `_strlen`, `_cplus_sc_invoke_safe_str_constraint_handler`)
are satisfied by the system C library and `libcplus_safecrt.a` at link time.

## ABI impact

Additive only. No existing symbol is removed, renamed or retyped. The two new
`cplus_v_*` entry points plus the two upstream `_uchk` helpers are added to the
checked runtime; no build script, compiler or runtime source was modified.

## Exact verification commands and results

Run from the repository root. `build/safe/config.h` and both link archives were
already present; if `build/safe/config.h` is missing, run
`sh scripts/build_safecrt.sh >/dev/null 2>&1` first.

### 1. Compile the vendored group

```sh
/usr/bin/clang -O2 -std=c99 -D__STDC_WANT_LIB_EXT1__=0 \
  -Ithird_party/safeclib/include -Ibuild/safe -Ithird_party/safeclib/src \
  -Istdlib/safe -Wno-macro-redefined \
  -c libc/src/safecrt_vendor/strstr_group.c -o /tmp/strstr_group.o
```

Result: exit `0`, no diagnostics.

### 2. Confirm prefixed symbols

```sh
nm -g /tmp/strstr_group.o
```

Result: `cplus_v__strstr_s_chk` and `cplus_v__strcasestr_s_chk` present as global
`T` symbols (full output in the Symbols section above).

### 3. Build the standalone test

```sh
/usr/bin/clang -O2 -std=c99 -Ilibc/install-include \
  tests/libc/test_vendor_strstr.c /tmp/strstr_group.o \
  build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
  -o /tmp/test_vendor_strstr
```

Result: exit `0`, no diagnostics.

### 4. Run under a 30-second timeout

```sh
timeout 30 /tmp/test_vendor_strstr
```

Result: exit `0`. Final line of stdout:

```
vendor_strstr: 129 0
```

129 checks, 0 failures. (The `IGNORE CONSTRAINT HANDLER: ...` lines also printed
by the test come from the no-op default constraint handler in
`libcplus_safecrt.a` and confirm the constraint-violation paths were exercised.)

Coverage includes: found / not-found, NULL `substringp` / `dest` / `src`,
`dmax = 0` (`ESZEROL`), `dmax` and `slen` one over `RSIZE_MAX_STR` (`ESLEMAX`),
`slen = 0` (with the strstr empty-src carve-out and the strcasestr `ESZEROL`
behavior), `dest == src`, one-over-`strlen` `dmax`, `slen > dmax` (`ESNOTFND` /
`ESLEMAX`), truncated `slen`, case-insensitive matching, and valid-input parity
against ordinary `strstr` (strstr) and a local ASCII case-insensitive oracle
(strcasestr).
