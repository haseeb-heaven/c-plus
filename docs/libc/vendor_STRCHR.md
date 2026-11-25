# Vendored safeclib `strchr_s` / `strrchr_s` into the checked runtime

## Provenance (recorded exactly)

- Upstream repository: `rurban/safeclib` <https://github.com/rurban/safeclib>
- Pinned commit: `90b076e94c4b0fb202dddf948d5d620e5395c1b0`
- License: MIT (`third_party/safeclib/COPYING`)
- Upstream sources, included verbatim:
  - `third_party/safeclib/src/extstr/strchr_s.c`
  - `third_party/safeclib/src/extstr/strrchr_s.c`

The upstream MIT copyright notice (September 2017, Reini Urban, Copyright (c)
2017 by Reini Urban, all rights reserved, with the full permission text) is
reproduced at the top of `libc/src/safecrt_vendor/strchr_group.c`.

## Verbatim inclusion — prefix-only changes

`libc/src/safecrt_vendor/strchr_group.c` does **not** reimplement or retype
either function. It `#include`s the two upstream translation units verbatim:

```c
/* provenance comment */
#define strchr_s cplus_v_strchr_s
#define _strchr_s_chk cplus_v__strchr_s_chk
#define strrchr_s cplus_v_strrchr_s
#define _strrchr_s_chk cplus_v__strrchr_s_chk
#include "extstr/strchr_s.c"
#include "extstr/strrchr_s.c"
```

No upstream source line is altered: algorithms, constraint ordering and return
codes are exactly those of the pinned commit. Only names are prefixed, so the
change is **ABI additive** — no existing symbol is replaced or removed.

### Extra name mappings needed for this group

The literal four-define pattern above compiles but leaves three unresolved
references, because this translation unit is built outside
`scripts/build_safecrt.sh` while the checked runtime exports those helpers only
under prefixed names. They are resolved with **preprocessor name mappings only**
(no function body and no upstream logic is added):

| Reference in upstream body | Mapped to | Where the target already lives |
| --- | --- | --- |
| `invoke_safe_str_constraint_handler` | `cplus_sc_invoke_safe_str_constraint_handler` | `build/safe/libcplus_safecrt.a` |
| `_strnlen_s_chk` | `cplus_sc__strnlen_s_chk` | `build/safe/libcplus_safecrt.a` |
| `_memrchr_s_chk(dest,dmax,ch,resultp,destbos)` | `cplus_sc_upstream_memrchr_s(dest,dmax,ch,resultp)` | `build/libc/libcplus_hardened.a` |

The last mapping drops `destbos` because the already-vendored runtime helper is
the 4-argument upstream port (`cplus_sc_upstream_memrchr_s`) rather than
safeclib's 5-argument `_memrchr_s_chk`; the search semantics (last match in the
first `dmax` bytes, `*resultp = NULL` on failure) are the same. The default
constraint handler is `ignore_handler_s`, so violations return the documented
error codes without aborting; the `IGNORE CONSTRAINT HANDLER: ...` lines in the
test output are expected.

### Symbols actually present in `strchr_group.o`

`nm -g --defined-only /tmp/strchr_group.o` shows four defined globals; the two
public `cplus_v_*` entry points are declared in
`libc/install-include/cplus_vendor_strchr.h`. The unprefixed `*_uchk` workers
come from the verbatim upstream bodies and are deliberately not part of the
public header surface.

## Files (only these four were created)

- `libc/src/safecrt_vendor/strchr_group.c`
- `libc/install-include/cplus_vendor_strchr.h`
- `tests/libc/test_vendor_strchr.c`
- `docs/libc/vendor_STRCHR.md` (this file)

No build scripts, compiler/runtime sources, git index or third-party files were
modified by this work. The parent wires the object into the build.

## Exact commands and results

All commands run from the repository root on branch `agent/modern-stdlib`.
`build/safe/config.h` and both static archives were already present
(`sh scripts/build_safecrt.sh` was attempted first but the ambient `clang` on
PATH has a missing `MacOSX27.sdk` sysroot; the object/test builds below use
`/usr/bin/clang`, which has a valid sysroot, and need no rebuild).

### 1. Compile the wrapper object

```
$ /usr/bin/clang -O2 -std=c99 -D__STDC_WANT_LIB_EXT1__=0 \
    -Ithird_party/safeclib/include -Ibuild/safe \
    -Ithird_party/safeclib/src -Istdlib/safe \
    -Wno-macro-redefined \
    -c libc/src/safecrt_vendor/strchr_group.c -o /tmp/strchr_group.o
(exit 0, no diagnostics)
```

### 2. Confirm `cplus_v_*` symbols

```
$ nm -g --defined-only /tmp/strchr_group.o
0000000000000114 T __strchr_s_uchk
0000000000000290 T __strrchr_s_uchk
0000000000000000 T _cplus_v__strchr_s_chk
000000000000016c T _cplus_v__strrchr_s_chk

$ nm -g -u /tmp/strchr_group.o
_cplus_sc__strnlen_s_chk
_cplus_sc_invoke_safe_str_constraint_handler
_cplus_sc_upstream_memrchr_s
_strchr
```

`_strchr` is the ordinary C library `strchr` and resolves from the system libc;
the other three resolve from the two static archives at link time.

### 3. Build the standalone test

```
$ /usr/bin/clang -O2 -std=c99 -Wall -Wextra -pedantic -Ilibc/install-include \
    tests/libc/test_vendor_strchr.c /tmp/strchr_group.o \
    build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
    -o /tmp/test_vendor_strchr
(exit 0, no diagnostics)
```

### 4. Run the test under `timeout 30`

```
$ timeout 30 /tmp/test_vendor_strchr
IGNORE CONSTRAINT HANDLER: (400) strchr_s: dest is null
IGNORE CONSTRAINT HANDLER: (400) strchr_s: resultp is null
IGNORE CONSTRAINT HANDLER: (401) strchr_s: dmax is 0
IGNORE CONSTRAINT HANDLER: (403) strchr_s: dmax exceeds max
IGNORE CONSTRAINT HANDLER: (403) strchr_s: ch exceeds max
IGNORE CONSTRAINT HANDLER: (400) strrchr_s: dest is null
IGNORE CONSTRAINT HANDLER: (400) strrchr_s: resultp is null
IGNORE CONSTRAINT HANDLER: (401) strrchr_s: dmax is 0
IGNORE CONSTRAINT HANDLER: (403) strrchr_s: dmax exceeds max
IGNORE CONSTRAINT HANDLER: (403) strrchr_s: ch exceeds max
vendor_strchr: 54 0
exit=0
```

## Test coverage

The 54 checks cover, for both entry points: valid first/last find and
not-found, termination-NUL search (including `strrchr_s`'s dmax+1 rule), dmax
bounding, NULL `dest`, NULL `resultp`, zero `dmax`, one-over `RSIZE_MAX_STR`,
`ch > 255`, and `resultp == NULL` on every failure. Valid-input results are
compared directly against ordinary `strchr` / `strrchr`.
