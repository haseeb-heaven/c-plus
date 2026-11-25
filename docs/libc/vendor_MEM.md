# Vendored safeclib `memchr_s` / `memccpy_s` into the checked runtime

## Provenance (recorded exactly)

- Upstream: **rurban/safeclib**, commit
  `90b076e94c4b0fb202dddf948d5d620e5395c1b0` (also recorded in
  `third_party/safeclib/UPSTREAM_COMMIT`).
- License: **MIT** (`third_party/safeclib/COPYING`) —
  Copyright (C) 2012, 2013 Cisco Systems; Copyright (C) 2017 Reini Urban;
  All rights reserved. The full copyright/permission notice is reproduced at
  the top of `libc/src/safecrt_vendor/mem_group.c`.
- Upstream sources:
  - `third_party/safeclib/src/extmem/memchr_s.c`
  - `third_party/safeclib/src/extmem/memccpy_s.c`

## Files delivered

| File | Purpose |
| --- | --- |
| `libc/src/safecrt_vendor/mem_group.c` | Prefix macros + verbatim `#include` of the two upstream translation units; thin additive public shims. |
| `libc/install-include/cplus_vendor_mem.h` | Declares the two public prefixed functions with their real signatures; includes `<safe_types.h>` for `errno_t`/`rsize_t`. |
| `tests/libc/test_vendor_mem.c` | Standalone C99 test (23 checks). |
| `docs/libc/vendor_MEM.md` | This record. |

No other files were created or modified. No build scripts, compiler files,
or existing runtime sources were touched.

## Symbol names changed (prefix only)

The upstream translation units are **included verbatim via the preprocessor**.
Only symbol names are changed, by prefixing:

```c
#define memchr_s cplus_v_memchr_s
#define _memchr_s_chk cplus_v__memchr_s_chk
#define memccpy_s cplus_v_memccpy_s
#define _memccpy_s_chk cplus_v__memccpy_s_chk
#include "extmem/memchr_s.c"
#include "extmem/memccpy_s.c"
```

The upstream constraint-handler references are likewise prefixed
(`invoke_safe_mem_constraint_handler` and
`invoke_safe_str_constraint_handler` → `cplus_sc_*`) so they resolve against
the already-checked `build/safe/libcplus_safecrt.a`. No upstream logic is
reimplemented or retyped.

Safeclib spells the public `memchr_s`/`memccpy_s` names as header macros, so
the upstream object files only emit the BOS-aware `_chk` entry points. Two
**thin forwarding shims** (`cplus_v_memchr_s`, `cplus_v_memccpy_s`) are added
at the bottom of `mem_group.c`; they do nothing but forward to the verbatim
upstream bodies with `BOS_UNKNOWN`, exactly as the safeclib header macros do.
They contain no reimplementation of upstream logic.

### ABI statement

- Upstream bodies are included **verbatim via preprocessor**; only symbol
  names changed (prefix).
- The change is **ABI additive only**: it introduces new `cplus_v_*` exports.
- **The new symbols do not replace any existing export.** No existing symbol
  was renamed, removed, or shadowed.

### Global symbols in `/tmp/mem_group.o` (`nm -g`)

```
00000000000002f0 T __memccpy_s_uchk
00000000000000fc T __memchr_s_uchk
                 U _bzero
                 U _cplus_sc_invoke_safe_mem_constraint_handler
                 U _cplus_sc_invoke_safe_str_constraint_handler
0000000000000138 T _cplus_v__memccpy_s_chk
0000000000000000 T _cplus_v__memchr_s_chk
00000000000003cc T _cplus_v_memccpy_s
00000000000003c4 T _cplus_v_memchr_s
                 U _mem_prim_set
                 U _memchr
```

New `cplus_v_*` exports: `cplus_v_memchr_s`, `cplus_v_memccpy_s`,
`cplus_v__memchr_s_chk`, `cplus_v__memccpy_s_chk`. The upstream-internal
`_memchr_s_uchk` / `_memccpy_s_uchk` are also emitted (unchanged names);
they do not collide with the checked libs because the static archive members
are only pulled in when an unresolved reference requires them.

## Verification (all commands run from repo root)

`build/safe/config.h`, `build/safe/libcplus_safecrt.a`, and
`build/libc/libcplus_hardened.a` were already present. If absent, generate
them with `sh scripts/build_safecrt.sh >/dev/null 2>&1` first.

### 1. Wrapper object compile (exact required command)

```sh
timeout 30 /usr/bin/clang -O2 -std=c99 -D__STDC_WANT_LIB_EXT1__=0 \
  -Ithird_party/safeclib/include -Ibuild/safe -Ithird_party/safeclib/src \
  -Istdlib/safe -Wno-macro-redefined \
  -c libc/src/safecrt_vendor/mem_group.c -o /tmp/mem_group.o
```

Exit code: **0**. No compiler diagnostics. `nm -g` output shown above.

### 2. Test compile

```sh
timeout 30 /usr/bin/clang -O2 -std=c99 -Wall -Wextra \
  -Ilibc/install-include -Ithird_party/safeclib/include -Ibuild/safe \
  -c tests/libc/test_vendor_mem.c -o /tmp/test_vendor_mem.o
```

Exit code: **0**. No compiler diagnostics.

### 3. Link

```sh
timeout 30 /usr/bin/clang -O2 -std=c99 \
  /tmp/test_vendor_mem.o /tmp/mem_group.o \
  build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
  -o /tmp/test_vendor_mem
```

Exit code: **0**. No linker diagnostics. All unresolved references
(`cplus_sc_invoke_safe_mem_constraint_handler`,
`cplus_sc_invoke_safe_str_constraint_handler`, `mem_prim_set`, `memchr`,
`bzero`) resolve from the checked libs and system libc.

### 4. Run

```sh
timeout 30 /tmp/test_vendor_mem
```

Exit code: **0**.

Exact stdout/stderr:

```
IGNORE CONSTRAINT HANDLER: (400) memchr_s: dest is null
IGNORE CONSTRAINT HANDLER: (400) memchr_s: resultp is null
IGNORE CONSTRAINT HANDLER: (401) memchr_s: dmax is 0
IGNORE CONSTRAINT HANDLER: (84) memchr_s: dmax exceeds dest
IGNORE CONSTRAINT HANDLER: (400) memccpy_s: dest is null
IGNORE CONSTRAINT HANDLER: (400) memccpy_s: src is null
IGNORE CONSTRAINT HANDLER: (401) memccpy_s: dmax is 0
IGNORE CONSTRAINT HANDLER: (406) memccpy_s: n exceeds max
IGNORE CONSTRAINT HANDLER: (406) memccpy_s: n exceeds max
IGNORE CONSTRAINT HANDLER: (84) memccpy_s: dmax exceeds dest
vendor_mem: 23 0
```

The `IGNORE CONSTRAINT HANDLER` lines are the expected safeclib default
handler messages for the deliberate negative cases; the exit status is 0
because all 23 checks passed (`vendor_mem: 23 0`). Error code 84 is this
platform's `EOVERFLOW` (from `<errno.h>`); the test checks against `EOVERFLOW`
symbolically, not a hard-coded number.

### 5. Header C++ smoke check (additional)

```sh
printf '#include "cplus_vendor_mem.h"\nint main(){return 0;}\n' > /tmp/vm_smoke.cpp
/usr/bin/clang++ -std=c++20 -Ilibc/install-include \
  -Ithird_party/safeclib/include -Ibuild/safe -fsyntax-only /tmp/vm_smoke.cpp
```

Exit code: **0**.

## Coverage summary

The test covers, for `memchr_s`: valid input (pointer and byte compared
against ordinary `memchr`), not-found (`ESNOTFND`), NULL `dest`, NULL
`resultp`, zero `dmax`, and one-over `dmax` via the explicit-BOS `_chk` entry
point (`EOVERFLOW`). For `memccpy_s`: valid input compared byte-for-byte
against ordinary `memccpy`, `n == 0`, NULL `dest`, NULL `src`, zero `dmax`,
`n > dmax`, not-found with insufficient space, and one-over `dmax` via the
explicit-BOS `_chk` entry point (`EOVERFLOW`).
