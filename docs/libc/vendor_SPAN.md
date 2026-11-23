# Vendored safeclib span helpers: `strspn_s` / `strcspn_s`

## Provenance

| Field | Value |
| --- | --- |
| Upstream | rurban/safeclib <https://github.com/rurban/safeclib> |
| Pinned commit | `90b076e94c4b0fb202dddf948d5d620e5395c1b0` |
| License | MIT — `third_party/safeclib/COPYING` |
| Included sources | `third_party/safeclib/src/extstr/strspn_s.c`, `third_party/safeclib/src/extstr/strcspn_s.c` |
| SHA-256 (strspn_s.c) | `bef557644f21304b101618acbc89fe9acda11d141b2b7d0ae09edfc6242bc2b7` |
| SHA-256 (strcspn_s.c) | `26884690504987eb9a10ae87d59a4bc4fde790e6e3f413b6c754fda93062b5e4` |

The MIT copyright notices of both upstream files are reproduced verbatim in
the included sources (Copyright (c) 2008-2011, 2013 by Cisco Systems, Inc).
Nothing from `third_party/` was modified.

## Verbatim upstream inclusion — prefix-only changes

`libc/src/safecrt_vendor/spn_group.c` does **not** reimplement these
functions. It `#include`s the two pinned upstream `.c` files verbatim:

```c
#define strspn_s  cplus_v_strspn_s
#define _strspn_s_chk  cplus_v__strspn_s_chk
#define strcspn_s  cplus_v_strcspn_s
#define _strcspn_s_chk  cplus_v__strcspn_s_chk

#define invoke_safe_str_constraint_handler \
    cplus_sc_invoke_safe_str_constraint_handler
#define invoke_safe_mem_constraint_handler \
    cplus_sc_invoke_safe_mem_constraint_handler

#include "extstr/strspn_s.c"
#include "extstr/strcspn_s.c"
```

The only local changes are symbol-name remapping:

* The four `#define`s above rename symbols. They are the documented pattern;
  the `strspn_s` / `strcspn_s` names appear only in upstream `FOR_DOXYGEN`
  declarations and doc comments, so they produce no object symbol.
* The two `invoke_safe_*_constraint_handler` mappings point at the already
  prefixed handlers exported by `build/safe/libcplus_safecrt.a`. This is the
  identical `-D` prefix `scripts/build_safecrt.sh` applies when building
  safeclib itself; no behavior is changed.

No algorithm, constraint check, bounds path, or error-code path was edited.
`-Wno-macro-redefined` is required because `safe_str_lib.h` redefines the
`strspn_s` / `strcspn_s` dispatch macros after our prefix defines.

**Additive ABI.** The exported entry points are new `cplus_v_*` names, so
nothing in the existing checked runtime is replaced. The public header
`libc/install-include/cplus_vendor_spn.h` declares only symbols actually
present in the object (the two prefixed `_chk` entry points); the unprefixed
upstream `_strspn_s_uchk` / `_strcspn_s_uchk` workers are internal and omitted.

## Exact commands and results

All commands run from the repo root. Default `clang` on PATH is Homebrew LLVM
with a missing sysroot, so the safeclib archive was regenerated with the
task-specified Apple compiler (`/usr/bin/clang`) selected via the build
script's supported `CPLUS_CLANG` variable.

### 1. Ensure `build/safe/config.h` and archive (result: exit 0)

```sh
sh scripts/build_safecrt.sh >/dev/null 2>&1          # with Apple clang on PATH: exit 1
CPLUS_CLANG=/usr/bin/clang sh scripts/build_safecrt.sh >/dev/null 2>&1   # exit 0
```

The bare first form fails in this environment because PATH `clang` reports
`fatal error: 'stdio.h' file not found` (missing MacOSX27.sdk sysroot). With
`CPLUS_CLANG=/usr/bin/clang` the script succeeds and
`build/safe/libcplus_safecrt.a` is produced alongside the already-present
`build/libc/libcplus_hardened.a`.

### 2. Compile the vendor group (result: exit 0, no diagnostics)

```sh
/usr/bin/clang -O2 -std=c99 -D__STDC_WANT_LIB_EXT1__=0 \
  -Ithird_party/safeclib/include -Ibuild/safe \
  -Ithird_party/safeclib/src -Istdlib/safe \
  -Wno-macro-redefined \
  -c libc/src/safecrt_vendor/spn_group.c -o /tmp/spn_group.o
```

### 3. Confirm `cplus_v_*` symbols with `nm -g`

```sh
$ nm -g /tmp/spn_group.o
0000000000000350 T __strcspn_s_uchk
00000000000001a0 T __strspn_s_uchk
                 U _cplus_sc_invoke_safe_mem_constraint_handler
                 U _cplus_sc_invoke_safe_str_constraint_handler
000000000000021c T _cplus_v__strcspn_s_chk
0000000000000000 T _cplus_v__strspn_s_chk
```

Leading `_` is macOS Mach-O C symbol decoration. The two `T cplus_v_*`
symbols are the new public entry points; the two `U` symbols are resolved by
`build/safe/libcplus_safecrt.a` at link time.

### 4. Compile the standalone C99 test (result: exit 0)

```sh
/usr/bin/clang -O2 -std=c99 -Ilibc/install-include \
  tests/libc/test_vendor_spn.c /tmp/spn_group.o \
  build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
  -o /tmp/test_vendor_spn
```

### 5. Run under `timeout 30` (result: exit 0)

```sh
$ timeout 30 /tmp/test_vendor_spn
IGNORE CONSTRAINT HANDLER: (400) strspn_s: dest is null
IGNORE CONSTRAINT HANDLER: (400) strspn_s: src is null
IGNORE CONSTRAINT HANDLER: (400) strspn_s: countp is null
IGNORE CONSTRAINT HANDLER: (400) strcspn_s: dest is null
IGNORE CONSTRAINT HANDLER: (400) strcspn_s: src is null
IGNORE CONSTRAINT HANDLER: (400) strcspn_s: countp is null
IGNORE CONSTRAINT HANDLER: (401) strspn_s: dmax is 0
IGNORE CONSTRAINT HANDLER: (401) strspn_s: slen is 0
IGNORE CONSTRAINT HANDLER: (401) strcspn_s: dmax is 0
IGNORE CONSTRAINT HANDLER: (401) strcspn_s: slen is 0
IGNORE CONSTRAINT HANDLER: (403) strspn_s: dmax exceeds max
IGNORE CONSTRAINT HANDLER: (403) strspn_s: slen exceeds dmax
IGNORE CONSTRAINT HANDLER: (403) strcspn_s: dmax exceeds max
IGNORE CONSTRAINT HANDLER: (403) strcspn_s: slen exceeds max
vendor_spn: 39 0
```

The `IGNORE CONSTRAINT HANDLER:` lines are emitted by the archive's default
constraint handler on the deliberate violation cases; they confirm the
upstream handler call sites are reached. The required summary line is
`vendor_spn: 39 0` (39 checks, 0 failures); process exit status is 0.

## Coverage

`tests/libc/test_vendor_spn.c` covers:

* **Valid span counts** — full match, zero-length prefix, all-match, `dmax`
  truncation, `slen` truncation (strspn); full prefix, zero-length prefix,
  no delimiter, single delimiter (strcspn).
* **Parity** — every valid case compares against ordinary `strspn` / `strcspn`.
* **NULL** — `dest`, `src`, and `countp` NULL for both helpers (`ESNULLP`, 400).
* **Zero bounds** — `dmax == 0` and `slen == 0` for both (`ESZEROL`, 401).
* **One-over bounds** — `dmax == RSIZE_MAX_STR + 1` and
  `slen == RSIZE_MAX_STR + 1` for both (`ESLEMAX`, 403).

All calls pass `BOS_UNKNOWN` for `destbos`/`srcbos` to exercise upstream's full
runtime constraint-check path.
