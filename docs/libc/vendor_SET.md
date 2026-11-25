# Vendored safeclib string-set group (`cplus_v_*`)

## Scope and provenance

Adds three already-existing safeclib functions to the checked C runtime:

| Public upstream name | Prefixed export | Upstream path |
| --- | --- | --- |
| `strnset_s` | `cplus_v__strnset_s_chk` (+ inert `cplus_v_strnset_s` macro name) | `third_party/safeclib/src/extstr/strnset_s.c` |
| `strset_s` | `cplus_v__strset_s_chk` (+ inert `cplus_v_strset_s` macro name) | `third_party/safeclib/src/extstr/strset_s.c` |
| `strnterminate_s` | `cplus_v__strnterminate_s_chk` (+ inert `cplus_v_strnterminate_s` macro name) | `third_party/safeclib/src/extstr/strnterminate_s.c` |

- Upstream: `rurban/safeclib` commit `90b076e94c4b0fb202dddf948d5d620e5395c1b0`
  (verified in `third_party/safeclib/UPSTREAM_COMMIT`).
- License: MIT, `third_party/safeclib/COPYING`. Copyright notices are
  reproduced verbatim in `libc/src/safecrt_vendor/set_group.c`
  (Cisco 2012–2013, Reini Urban 2017; `strnterminate_s.c` is Cisco 2011).

## Verbatim inclusion, prefix-only changes, additive ABI

`libc/src/safecrt_vendor/set_group.c` contains **no reimplementation**. It uses
the preprocessor to rename symbols and then `#include`s the three upstream
translation units verbatim:

```c
#define strnset_s cplus_v_strnset_s
#define _strnset_s_chk cplus_v__strnset_s_chk
#define strset_s cplus_v_strset_s
#define _strset_s_chk cplus_v__strset_s_chk
#define strnterminate_s cplus_v_strnterminate_s
#define _strnterminate_s_chk cplus_v__strnterminate_s_chk
#define invoke_safe_str_constraint_handler \
    cplus_sc_invoke_safe_str_constraint_handler
#include "extstr/strnset_s.c"
#include "extstr/strset_s.c"
#include "extstr/strnterminate_s.c"
```

The seventh define is also a prefix-only rename: it routes the vendored
constraint-handler call to the already-prefixed symbol exported by
`build/safe/libcplus_safecrt.a` (the archive produced by
`scripts/build_safecrt.sh`). Upstream algorithms, constraint checks, error
codes and handler behavior are otherwise untouched.

All new externally visible names are `cplus_v_*`; no existing export is
modified, removed or shadowed, so the ABI is **additive**. Safeclib spells the
plain `strnset_s`/`strset_s`/`strnterminate_s` names as header macros, so only
the three BOS-aware `_chk` entry points are real symbols in the object; the
unprefixed upstream `_strnset_s_uchk`, `_strset_s_uchk` and
`_strnterminate_s_uchk` workers are internal helpers and are intentionally
omitted from the public header `libc/install-include/cplus_vendor_set.h`.

## Verification (exact commands and results)

Run from the repository root. The config header and archives are prerequisites;
`sh scripts/build_safecrt.sh >/dev/null 2>&1` regenerates them (on this host that
script's own `clang` on `PATH` has a broken `MacOSX27.sdk` sysroot and exits 1,
but the prebuilt `build/safe/config.h`, `build/safe/libcplus_safecrt.a` and
`build/libc/libcplus_hardened.a` are present and the required compile command
below uses `/usr/bin/clang` directly).

```sh
$ timeout 30 sh scripts/build_safecrt.sh >/dev/null 2>&1
  # exit 1 (host SDK issue; artifacts already present, config.h regenerated)

$ timeout 30 /usr/bin/clang -O2 -std=c99 -D__STDC_WANT_LIB_EXT1__=0 \
    -Ithird_party/safeclib/include -Ibuild/safe \
    -Ithird_party/safeclib/src -Istdlib/safe \
    -Wno-macro-redefined -c libc/src/safecrt_vendor/set_group.c \
    -o /tmp/set_group.o
  # exit 0, no diagnostics

$ nm -g /tmp/set_group.o
0000000000000104 T __strnset_s_uchk
0000000000000228 T __strnterminate_s_uchk
0000000000000208 T __strset_s_uchk
                 U _cplus_sc_invoke_safe_str_constraint_handler
0000000000000000 T _cplus_v__strnset_s_chk
0000000000000274 T _cplus_v__strnterminate_s_chk
0000000000000124 T _cplus_v__strset_s_chk

$ timeout 30 /usr/bin/clang -O2 -std=c99 -Ilibc/install-include \
    tests/libc/test_vendor_set.c /tmp/set_group.o \
    build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
    -o /tmp/test_vendor_set
  # exit 0

$ timeout 30 /tmp/test_vendor_set
vendor_set: 48 0
  # exit 0
```

A stricter compile (`-Wall -Wextra -pedantic -Wno-unused-parameter
-Wno-macro-redefined`, same includes) also exits 0; its 176 diagnostics come
only from `diagnose_if` extensions in the pinned `third_party/safeclib`
headers, not from these files. The installed header also passes a
`clang++ -std=c++17 -fsyntax-only` smoke compile.

## Test coverage

`tests/libc/test_vendor_set.c` (48 checks) covers:

- **Fill semantics** — full fill, `dmax`-capped fill, `n`-capped fill, `n == 0`,
  and stopping at an embedded NUL.
- **Exact fit** — `dmax == strlen + 1`, `n == dmax`, and known `destbos`
  equal to `dmax` for both setters and the terminator.
- **One-over** — `n > dmax` (`ESNOSPC`), `dmax > RSIZE_MAX_STR` (`ESLEMAX`),
  `value > 255` (`ESLEMAX`), and known-`destbos` overflow (`EOVERFLOW`).
- **NULL / zero** — NULL dest for all three helpers, and `dmax == 0`
  (`ESZEROL` / `0`).
- **Termination behavior** — no NUL within `dmax` (writes at `dmax-1`),
  `dmax == strlen`, early NUL, and the `dmax == 2` / `dmax == 1` boundaries.

Note: upstream returns the platform `EOVERFLOW`. The public header maps
`CPLUS_V_SET_EOVERFLOW` to `<errno.h>`'s `EOVERFLOW` (84 on macOS, 75 fallback)
instead of hard-coding 75, so the documented contract matches the object on
both hosts.
