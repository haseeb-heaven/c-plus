# External SDK consumer proof (`find_package`)

This test proves that an installed C+ hardened libc can be consumed by a
completely separate CMake project using `find_package`, with **no source-tree
paths**, and that **unchanged ordinary C spellings** (`malloc`, `memset`,
`memcpy`, `strcpy`, `free`) work from the installed headers.

## Files

- `tests/sdk/consumer/CMakeLists.txt` — external consumer project.
- `tests/sdk/consumer/main.c` — ordinary-C-only consumer program.
- `tests/sdk/run_sdk_consumer.sh` — end-to-end runner (install + consume + run).

## Exact commands

```sh
# From the repository root:
sh tests/sdk/run_sdk_consumer.sh
```

The runner performs these steps (all temp dirs are removed on exit):

```sh
ROOT=$(pwd)
CC=${CPLUS_CLANG:-clang}
WORK=$(mktemp -d "${TMPDIR:-/tmp}/cplus-sdk-consumer.XXXXXX")
PREFIX="$WORK/install-prefix"

# 1. Build + install the SDK into a clean prefix.
cmake -S "$ROOT/libc" -B "$WORK/libc-build" -G "Unix Makefiles" \
  -DCMAKE_C_COMPILER="$CC" -DCMAKE_INSTALL_PREFIX="$PREFIX"
cmake --build "$WORK/libc-build"
cmake --install "$WORK/libc-build"

# 2. Configure + build the external consumer against ONLY the install prefix.
cmake -S "$ROOT/tests/sdk/consumer" -B "$WORK/external-consumer-build" \
  -G "Unix Makefiles" -DCMAKE_C_COMPILER="$CC" -DCMAKE_PREFIX_PATH="$PREFIX"
cmake --build "$WORK/external-consumer-build"

# 3. Run and assert the exact stdout line.
"$WORK/external-consumer-build/sdk_consumer"   # prints "sdk consumer: ok"
```

Set `CPLUS_CLANG` to override the compiler used for both builds:

```sh
CPLUS_CLANG=/path/to/clang sh tests/sdk/run_sdk_consumer.sh
```

## What is proven

1. **Installation** — `libc/CMakeLists.txt` installs both archives, the public
   headers, the CMake package and the contracts into a clean prefix.
2. **Export** — `cplus_hardened_libcConfig.cmake` and
   `cplus_hardened_libcTargets.cmake` are installed under
   `lib/cmake/cplus_hardened_libc`; the `cplus::cplus_hardened_libc` imported
   target carries `INSTALL_INTERFACE:include/cplus-libc` as its include dir.
3. **Discovery** — `find_package(cplus_hardened_libc REQUIRED CONFIG)` succeeds
   using only `CMAKE_PREFIX_PATH`, with no source-tree include or library
   paths in the consumer.
4. **Ordinary spellings** — `main.c` includes only the installed
   `cplus_libc_safe.h` and calls plain C names. There is no `_s` spelling, no
   `cplus_*`-prefixed call in the program body, and no SafeCRT dependency.
5. **Runtime behaviour** — an in-bounds `memset`/`memcpy` succeeds, while
   over-capacity `memset` (16 bytes remaining, 32 requested), `memcpy` (4
   remaining, 8 requested) and `strcpy` (capacity 4, needs 8) are all refused
   with `errno == ERANGE` and no crash. The process exits 0 and prints exactly
   `sdk consumer: ok`.

Note: the consumer also calls `find_package(Threads REQUIRED)` because the
exported static target's `LINK_ONLY` interface references `Threads::Threads`
(the plain `m` library needs no separate target). This is a normal downstream
requirement, not a source-tree dependency.

## Limitations / exact failure when ordinary headers are not installed

The consumer includes `<cplus_libc_safe.h>`, which itself includes
`"cplus_libc.h"`. Both must be present under
`<prefix>/include/cplus-libc/`. If the ordinary-C headers have not yet been
added to the install rules, step 2 fails at **build** time (configure still
succeeds, because the exported target and config are installed). The runner
prints `sdk consumer: FAIL: external consumer build failed` followed by the
build log containing, on Clang:

```
.../tests/sdk/consumer/main.c:8:10: fatal error: 'cplus_libc_safe.h' file not found
    8 | #include <cplus_libc_safe.h>
      |          ^~~~~~~~~~~~~~~~~~~~
1 error generated.
```

Equivalently, if `cplus_libc_safe.h` is installed but `cplus_libc.h` is not,
the error is:

```
.../include/cplus-libc/cplus_libc_safe.h:24:10: fatal error: 'cplus_libc.h' file not found
```

This failure is intentional: the test is not weakened to pass when the
headers are missing. The fix belongs in `libc/CMakeLists.txt` (install the
header set into `include/cplus-libc`), after which this runner must pass
unchanged.

Other honest limits (see `docs/libc/ORDINARY_C_SAFETY.md`): capacity is known
for live `malloc` blocks (including interior/one-past-end pointers) and for
stack/global objects where `__builtin_object_size` applies; genuinely unknown
capacity is unchecked in default mode unless strict mode is enabled.
