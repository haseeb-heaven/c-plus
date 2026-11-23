# Vendored safeclib case conversion (`strtolowercase_s`, `strtouppercase_s`)

## Provenance

- Upstream: `rurban/safeclib` <https://github.com/rurban/safeclib>
- Pinned commit: `90b076e94c4b0fb202dddf948d5d620e5395c1b0`
  (`third_party/safeclib/UPSTREAM_COMMIT`)
- License: MIT, `third_party/safeclib/COPYING`
  (Copyright (C) 2012, 2013 Cisco Systems; Copyright (C) 2017 Reini Urban).
  The individual upstream translation units carry
  `Copyright (c) 2008-2011 by Cisco Systems, Inc` and their MIT grant; those
  notices are reproduced verbatim in `libc/src/safecrt_vendor/case_group.c`.
- Upstream sources (hashes at the pin, recorded as evidence):

  | file | sha256 |
  | --- | --- |
  | `third_party/safeclib/src/extstr/strtolowercase_s.c` | `d001c0c804f54031c658e867ee606439705181aaa55974db13d663c6d4dab13d` |
  | `third_party/safeclib/src/extstr/strtouppercase_s.c` | `b93dacb4ed6af12d674dc839b5f897643cf0718fb93d246facbc95ecae4653ec` |

## Verbatim upstream inclusion — no reimplementation

`libc/src/safecrt_vendor/case_group.c` does **not** reimplement either
function. It `#include`s the two pinned upstream translation units verbatim:

```c
#include "extstr/strtolowercase_s.c"
#include "extstr/strtouppercase_s.c"
```

The include path comes from the project's own `-Ithird_party/safeclib/src`
flag, so the compiler reads the checked-in upstream files byte-for-byte; no
copy of the function bodies exists in the wrapper.

### Prefix-only changes

The only edits around the included sources are symbol renames (plus a handler
name remap, described below). No upstream algorithm, constraint ordering,
error code, or source line is altered:

```c
#define strtolowercase_s cplus_v_strtolowercase_s
#define _strtolowercase_s_chk cplus_v__strtolowercase_s_chk
#define strtouppercase_s cplus_v_strtouppercase_s
#define _strtouppercase_s_chk cplus_v__strtouppercase_s_chk

/* Bind the upstream constraint handler to the C+ safecrt handler already
 * exported by libcplus_safecrt.a; this is a name remap only. */
#define invoke_safe_str_constraint_handler                                     \
    cplus_sc_invoke_safe_str_constraint_handler
```

The handler remap follows the approach already documented for the timingsafe
sources in `third_party/safeclib/PROVENANCE.md`: the C+ build prefixes
`invoke_safe_str_constraint_handler` when it compiles safeclib, so the bare
upstream name is not exported by `libcplus_safecrt.a`. Without the remap the
object would carry an unsatisfiable undefined reference; with it, the verbatim
upstream bodies call the same C+ constraint handler as the rest of the
checked runtime. No handler behavior is changed.

## Additive ABI

All new externally visible names are new `cplus_v_*` symbols; nothing in the
existing runtime is replaced, renamed, or removed. `nm -g` on the compiled
object shows exactly:

```
0000000000000000 T __strtolowercase_s_uchk
0000000000000144 T __strtouppercase_s_uchk
                 U _cplus_sc_invoke_safe_str_constraint_handler
0000000000000048 T _cplus_v__strtolowercase_s_chk
0000000000000190 T _cplus_v__strtouppercase_s_chk
```

Mach-O prefixes C symbols with `_`, so the exported C names are:

- `cplus_v__strtolowercase_s_chk` — `int (char *dest, size_t dmax, const size_t destbos)`
- `cplus_v__strtouppercase_s_chk` — `int (char *dest, size_t dmax, const size_t destbos)`
- `_strtolowercase_s_uchk`, `_strtouppercase_s_uchk` — unprefixed upstream
  internal helpers from the verbatim sources; not part of the new public API.

There is deliberately **no** `cplus_v_strtolowercase_s` / `cplus_v_strtouppercase_s`
symbol: at the pin, safeclib defines those public names as function-like
macros over the checked forms in `include/safe_str_lib.h`, so verbatim
inclusion emits only the `_chk` entry points. `libc/install-include/cplus_vendor_case.h`
therefore declares only the two prefixed symbols that are actually present in
the object, with their real three-argument signatures and self-contained
`size_t`/`int` spellings of `rsize_t`/`errno_t`.

## Files

- `libc/src/safecrt_vendor/case_group.c` — verbatim upstream inclusion, prefix-only renames.
- `libc/install-include/cplus_vendor_case.h` — declarations for the two prefixed symbols.
- `tests/libc/test_vendor_case.c` — standalone C99 test.
- `docs/libc/vendor_CASE.md` — this record.

No build scripts, compiler/runtime sources, or other files were modified. The
object is additive and is not yet wired into the build by this change; the
parent integration owns that step.

## Verification — exact commands and results

All commands run from the repository root on branch `agent/modern-stdlib`.

### 1. Ensure generated safeclib config

```
$ sh scripts/build_safecrt.sh >/dev/null 2>&1
$ test -f build/safe/config.h && echo present
present
```

Host caveat (recorded for honesty): in this environment
`sh scripts/build_safecrt.sh` exits 1 after generating `config.h`, because
`/usr/bin/clang` (Apple clang 16) looks for a missing `MacOSX27.sdk`:

```
clang: warning: no such sysroot directory: '/Library/Developer/CommandLineTools/SDKs/MacOSX27.sdk' [-Wmissing-sysroot]
.../safeclib_private.h:98:10: fatal error: 'stdio.h' file not found
1 error generated.
```

The generated `config.h` is complete, and the pre-existing
`build/safe/libcplus_safecrt.a` / `build/libc/libcplus_hardened.a` archives are
intact, so the object compile and the standalone test are unaffected. Passing
`-isysroot "$(xcrun --show-sdk-path)"` works around the host SDK issue.

### 2. Compile the vendor group

```
$ /usr/bin/clang -O2 -std=c99 -D__STDC_WANT_LIB_EXT1__=0 \
    -Ithird_party/safeclib/include -Ibuild/safe -Ithird_party/safeclib/src \
    -Istdlib/safe -Wno-macro-redefined \
    -c libc/src/safecrt_vendor/case_group.c -o /tmp/case_group.o
compile_exit=0
```

### 3. Confirm symbols

```
$ nm -g /tmp/case_group.o
0000000000000000 T __strtolowercase_s_uchk
0000000000000144 T __strtouppercase_s_uchk
                 U _cplus_sc_invoke_safe_str_constraint_handler
0000000000000048 T _cplus_v__strtolowercase_s_chk
0000000000000190 T _cplus_v__strtouppercase_s_chk
```

### 4. Build and run the standalone test (timeout 30)

```
$ /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -Ilibc/install-include \
    tests/libc/test_vendor_case.c \
    /tmp/case_group.o build/safe/libcplus_safecrt.a \
    build/libc/libcplus_hardened.a -o /tmp/test_vendor_case
link_exit=0

$ timeout 30 /tmp/test_vendor_case
IGNORE CONSTRAINT HANDLER: (400) strtolowercase_s: dest is null
IGNORE CONSTRAINT HANDLER: (400) strtouppercase_s: dest is null
IGNORE CONSTRAINT HANDLER: (401) strtolowercase_s: dmax is 0
IGNORE CONSTRAINT HANDLER: (401) strtouppercase_s: dmax is 0
IGNORE CONSTRAINT HANDLER: (403) strtolowercase_s: dmax exceeds max
IGNORE CONSTRAINT HANDLER: (403) strtouppercase_s: dmax exceeds max
IGNORE CONSTRAINT HANDLER: (84) strtolowercase_s: dmax exceeds dest
IGNORE CONSTRAINT HANDLER: (84) strtolowercase_s: dmax exceeds dest
vendor_case: 28 0
run_exit=0
```

The `IGNORE CONSTRAINT HANDLER` lines are emitted by the safeclib default
ignore handler on the deliberate constraint-violation cases; the required
summary line is `vendor_case: 28 0` and the exit status is 0.

### 5. Test coverage

28 checks across nine groups:

- lowercase conversion correctness (`AbC-XYz 09_Mn` -> `abc-xyz 09_mn`);
- uppercase conversion correctness (`aBc-dEf 12_gh` -> `ABC-DEF 12_GH`);
- stop at first embedded NUL;
- stop at `dmax` when no NUL is present in the range;
- non-alphabetic passthrough (digits, space, punctuation unchanged);
- NULL destination -> `ESNULLP` (400), both functions;
- `dmax == 0` -> `ESZEROL` (401), both functions;
- `dmax == RSIZE_MAX_STR + 1` -> `ESLEMAX` (403), both functions;
- `dmax == destbos + 1` -> platform `EOVERFLOW` (84), both functions,
  with the destination verified unchanged on every violation.

### 6. Additional checks

- Header is self-contained and compiles clean as C99 and as C++11
  (`-Wall -Wextra -pedantic`, `extern "C"` guards).
- An ASan/UBSan build compiled, but the sanitizer runtime itself aborts at
  startup on this host
  (`AddressSanitizer: CHECK failed: sanitizer_malloc_mac.inc:189`), the same
  Apple-clang/missing-SDK environment issue as step 1 — not a finding about
  the vendored code. The unsanitized test above passes under the required
  30-second timeout.
