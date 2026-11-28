# Consumer matrix — standard C headers and checked-layer overlays

This lab verifies that the ordinary ISO C headers and the two additive
checked-layer overlays compile **unchanged** for both C and C++ consumers.
Each header gets a tiny `.c` consumer and a tiny `.cpp` consumer under
`consumers/`. `run.sh` compiles all of them with a 30-second timeout per run
and writes the exact commands, exit codes and diagnostics to `RESULTS.md`.

## Header sources

| Scope | Header(s) | Where it comes from |
| --- | --- | --- |
| Standard C | `stdlib.h`, `string.h`, `stdio.h`, `stdint.h`, `stddef.h`, `errno.h`, `ctype.h`, `limits.h`, `assert.h` | SDK system headers (/usr/bin/clang 16, macOS). The repo has no install-include copies of these names; `libc/install-include/` contains only additive `cplus_*` overlays. Upstream musl copies exist under `libc/upstream/musl/include/` for the cross-build, but the consumer-facing spellings are unchanged. |
| Additive overlay | `cplus_libc_safe.h`, `cplus_libc.h` (transitive) | `libc/install-include/` (installed to `include/cplus-libc/`). |
| Additive overlay | `stdlib/safe/safe_c.h` + transitive `safe_alloc.h`, `safe_memory.h`, `safe_string.h`, `safe_io.h`, `safe_format.h`, `cplus_libc.h`, `cplus_safecrt_upstream.h` | `stdlib/safe/` and `libc/install-include/`. |

`cksum` confirms the `cplus_libc.h` / `cplus_libc_safe.h` copies in
`build/libc-install/include/cplus-libc/` and
`build/compat/prefix/include/cplus-libc/` are byte-identical to the
`libc/install-include/` originals, so the results apply to the installed
prefix as well.

## Per-header coverage

| Header | Consumer files | Declarations exercised (both languages) |
| --- | --- | --- |
| `stdlib.h` | `stdlib.c`, `stdlib.cpp` | `malloc`, `free`, `abs` |
| `string.h` | `string.c`, `string.cpp` | `memset`, `strlen`, `memcpy` |
| `stdio.h` | `stdio.c`, `stdio.cpp` | `snprintf`, `fputs`, `stdout`, `EOF` |
| `stdint.h` | `stdint.c`, `stdint.cpp` | `uint32_t`, `int32_t`, `uint64_t`, `UINT32_C`, `INT32_C`, `UINT64_C` |
| `stddef.h` | `stddef.c`, `stddef.cpp` | `size_t`, `NULL`, `offsetof` |
| `errno.h` | `errno.c`, `errno.cpp` | `errno` (assignable lvalue), `EINVAL`, `EDOM` |
| `ctype.h` | `ctype.c`, `ctype.cpp` | `isalpha`, `isdigit`, `toupper`, `isspace` |
| `limits.h` | `limits.c`, `limits.cpp` | `CHAR_BIT`, `INT_MAX`, `UINT_MAX`, `LONG_MAX` |
| `assert.h` | `assert.c`, `assert.cpp` | `assert` |
| `cplus_libc_safe.h` | `cplus_libc_safe.c`, `cplus_libc_safe.cpp` | ordinary `malloc` / `memset` / `free` spellings (macro-redirected by the overlay) plus explicit `cplus_libc_set_unknown_capacity_mode` |
| `stdlib/safe/safe_c.h` | `safe_c.c`, `safe_c.cpp` | ordinary `malloc` / `memset` / `free` spellings (macro-redirected to `malloc_s` / `cplus_memset_s_auto` / `free_s`) |
| all nine standard headers together | `all_std.c`, `all_std.cpp` | cross-header inclusion in one translation unit (no conflict) |

Supplementary C++17 isolation probes (not in the 24-run main matrix):

| Probe | Purpose |
| --- | --- |
| `safe_headers_only.cpp` | Includes the safeclib-style `safe_*.h` headers directly (no `cplus_libc.h`) and calls `malloc_s`, `cplus_memset_s_auto`, `cplus_strcpy_s_auto`, `free_s`. Expected PASS: proves the safeclib-style overlay is C++-parseable and its declarations have C linkage. |
| `cplus_libc_only.cpp` | Includes `cplus_libc.h` alone. Expected FAIL: reproduces the exact diagnostics of both failing overlay consumers and isolates the root cause. |

## Compile flags

Exactly the requested warning flags are used; the two `-I` flags only make the
in-repo overlay headers reachable and never shadow a standard header name.

```text
CC  = ${CPLUS_CLANG:-/usr/bin/clang}
CXX = ${CPLUS_CXX:-/usr/bin/clang++}

$CC  -std=c99   -Wall -Wextra -pedantic -I<root> -I<root>/libc/install-include -c <consumer.c>   -o <artifact.o>
$CXX -std=c++17 -Wall -Wextra -pedantic -I<root> -I<root>/libc/install-include -c <consumer.cpp> -o <artifact.o>
timeout 30 on every compile
```

## What is being asserted

1. **Ordinary C spellings and signatures are preserved.** Every consumer uses
   the plain ISO C names and call shapes (`malloc(8)`, `strlen("abc")`,
   `snprintf(buf, sizeof buf, ...)`, `offsetof(...)`, `errno = 0`, and so on).
   No consumer is rewritten to a `_s` spelling or a `cplus_*` name in order to
   compile against a standard header.
2. **The checked layer is additive / opt-in.** None of the nine standard
   headers pulls in `cplus_libc_safe.h` or `stdlib/safe/safe_c.h`. The macro
   redirections exist only after an explicit `#include` of an overlay (and can
   be suppressed with `CPLUS_LIBC_SAFE_DISABLE` / `CPLUS_SAFE_C_DISABLE`). The
   `all_std.*` consumers compile with no overlay in scope.
3. **`extern "C"` is present where needed.** Every header that declares a
   function carries an `extern "C"` guard (audited in `RESULTS.md`). The two
   top-level overlays are macro-only and declare no functions, so they need no
   guard of their own.
4. **Genuine gaps are reported, not hidden.** See `RESULTS.md` for the exact
   compiler diagnostics of the two C++17 overlay failures and the isolation
   probe that identifies `cplus_libc.h` as the sole blocker (C-only `restrict`
   and `double complex` tokens). No header was edited to make a test pass.

## Run

```sh
sh tests/abi/stdc_consumers/run.sh
```

Exit status is 1 while any main-matrix row FAILs, 0 when all 24 compile. All
generated evidence stays under `tests/abi/stdc_consumers/artifacts/`; the
script performs no git add/commit and edits no header.
