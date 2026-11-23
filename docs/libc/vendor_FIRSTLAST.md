# Vendored safeclib first/last char + diff group (`firstlast_group`)

## Provenance (exact)

| Field | Value |
| --- | --- |
| Upstream | rurban/safeclib <https://github.com/rurban/safeclib> |
| Pinned commit | `90b076e94c4b0fb202dddf948d5d620e5395c1b0` |
| License | MIT — `third_party/safeclib/COPYING` |
| Included source 1 | `third_party/safeclib/src/extstr/strfirstchar_s.c` |
| Included source 2 | `third_party/safeclib/src/extstr/strlastchar_s.c` |
| Included source 3 | `third_party/safeclib/src/extstr/strfirstdiff_s.c` |
| Included source 4 | `third_party/safeclib/src/extstr/strlastdiff_s.c` |

Copyright notices are reproduced verbatim in
`libc/src/safecrt_vendor/firstlast_group.c` (lines naming Bo Berry /
Cisco Systems, Inc., 2008-2011 and, for `strlastdiff_s.c`, 2013).

## Verbatim inclusion / changes made

The four upstream translation units are pulled into the checked runtime with
`#include` **verbatim**; no function body, loop, constraint check, error code
or comment from upstream was retyped or modified. The only local edits are
prefix-only `#define`s:

```c
#define strfirstchar_s cplus_v_strfirstchar_s
#define _strfirstchar_s_chk cplus_v__strfirstchar_s_chk
#define strlastchar_s cplus_v_strlastchar_s
#define _strlastchar_s_chk cplus_v__strlastchar_s_chk
#define strfirstdiff_s cplus_v_strfirstdiff_s
#define _strfirstdiff_s_chk cplus_v__strfirstdiff_s_chk
#define strlastdiff_s cplus_v_strlastdiff_s
#define _strlastdiff_s_chk cplus_v__strlastdiff_s_chk

#include "extstr/strfirstchar_s.c"
#include "extstr/strlastchar_s.c"
#include "extstr/strfirstdiff_s.c"
#include "extstr/strlastdiff_s.c"
```

In the non-`FOR_DOXYGEN` build the plain names appear only in upstream's
doxygen prototypes (not compiled) and in doxygen/comment text, so the
emitted entry points are the four `_chk` functions. The ABI is **additive**:
`cplus_v__strfirstchar_s_chk`, `cplus_v__strlastchar_s_chk`,
`cplus_v__strfirstdiff_s_chk` and `cplus_v__strlastdiff_s_chk` are new names
and no existing symbol is replaced or removed.

### Helper collisions and redirects

- All four upstream bodies call safeclib's constraint handler on a
  runtime-constraint violation. The checked runtime's archive exports that
  handler under the build script's `-D` prefix, so the identical link-time
  redirect is applied (no behavior change):

  ```c
  #define invoke_safe_str_constraint_handler \
      cplus_sc_invoke_safe_str_constraint_handler
  ```

  Without it the object would reference the unprefixed upstream handler,
  which is not present in `build/safe/libcplus_safecrt.a` (only
  `cplus_sc_invoke_safe_str_constraint_handler` and
  `cplus_upstream_invoke_safe_str_constraint_handler` are exported there).

- The upstream `_str*_s_uchk` workers are emitted with their original
  unprefixed names. They are self-contained (their only undefined reference
  is the redirected constraint handler) and **do not collide**: neither
  `build/safe/libcplus_safecrt.a` nor `build/libc/libcplus_hardened.a`
  defines them, and no other `libc/src/safecrt_vendor/*_group.c` defines or
  uses them. They are deliberately omitted from the public header as
  translation-unit internals.

## Exact verification commands and results

Run from the repository root (`/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib`).

1. Ensure the generated safeclib config (and archive) exist:

   ```sh
   sh scripts/build_safecrt.sh >/dev/null 2>&1
   ```

   `build/safe/config.h` is regenerated successfully. In this environment the
   script's later compile step fails because plain `clang` selects a missing
   `MacOSX27.sdk` sysroot (`fatal error: 'stdio.h' file not found` in
   `third_party/safeclib/src/extmem/timingsafe_bcmp.c`), so the already-built
   `build/safe/libcplus_safecrt.a` was used for linking. All vendor compiles
   below use `/usr/bin/clang`, which has a working sysroot.

2. Compile the vendor object (exact command):

   ```sh
   /usr/bin/clang -O2 -std=c99 -D__STDC_WANT_LIB_EXT1__=0 \
     -Ithird_party/safeclib/include -Ibuild/safe \
     -Ithird_party/safeclib/src -Istdlib/safe \
     -Wno-macro-redefined \
     -c libc/src/safecrt_vendor/firstlast_group.c -o /tmp/firstlast_group.o
   ```

   Result: exit 0, no diagnostics.

3. Confirm symbols (`nm -g /tmp/firstlast_group.o`, Mach-O leading `_`):

   ```text
   0000000000000104 T __strfirstchar_s_uchk
   00000000000003dc T __strfirstdiff_s_uchk
   0000000000000268 T __strlastchar_s_uchk
   0000000000000554 T __strlastdiff_s_uchk
   0000000000000000 T _cplus_v__strfirstchar_s_chk
   00000000000002c0 T _cplus_v__strfirstdiff_s_chk
   000000000000015c T _cplus_v__strlastchar_s_chk
   0000000000000428 T _cplus_v__strlastdiff_s_chk
   ```

   Undefined symbols (`nm -u`): only
   `_cplus_sc_invoke_safe_str_constraint_handler`, resolved from the safecrt
   archive at link time.

4. Link the standalone test:

   ```sh
   /usr/bin/clang -O2 -std=c99 -Wall -Wextra -pedantic \
     -Ilibc/install-include \
     tests/libc/test_vendor_firstlast.c /tmp/firstlast_group.o \
     build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
     -o /tmp/test_vendor_firstlast
   ```

   Result: exit 0, no diagnostics.

5. Run under a 30-second timeout:

   ```sh
   timeout 30 /tmp/test_vendor_firstlast
   ```

   Result: exit 0; final line:

   ```text
   vendor_firstlast: 1422 0
   ```

   (1422 checks, 0 failures.) The `IGNORE CONSTRAINT HANDLER: ...` lines on
   stderr are the runtime's default no-op/ignore handler exercising the
   intentional NULL, zero-bound, one-over-bound and over-max cases. Note that
   some of those messages read `strfirstchar_s: ...` or
   `strfirstdiff_s: ...` even when the failing call is `strlastdiff_s` — that
   is verbatim upstream message-text behavior and was intentionally not
   corrected.

## Test coverage

- Found and not-found indices for all four functions (including
  prefix-of-other and no-difference cases).
- NULL `dest`, `src` and out-pointer constraints for all four functions.
- `dmax == 0` (`ESZEROL`), `dmax == sizeof(dest)+1` with known `destbos`
  (`EOVERFLOW` — system errno, 84 on this macOS host, 75 on Linux), and
  `dmax == RSIZE_MAX_STR+1` with unknown object size (`ESLEMAX`).
- Parity with local reference loops over a corpus of strings/chars and string
  pairs, for multiple `dmax` values (including values past the terminating
  NUL), comparing both return codes and yielded indices/pointers.
- Public header self-containment checked with `/usr/bin/clang++ -std=c++11
  -Wall -Wextra -pedantic -fsyntax-only` (extern "C" block compiles cleanly).
