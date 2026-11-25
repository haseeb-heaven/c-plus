# `stdlib/safe` — optional checked C standard-library layer

This directory provides **optional** checked APIs alongside the unchanged C
standard library. It does **not** replace libc, does **not** rename existing
libc symbols, and does **not** claim that ordinary C becomes memory-safe.

## Goals

- Preserve familiar C names/contracts where platform/Annex K-style `_s` APIs
  exist (`fopen_s`, `sprintf_s`, mem/str adapters backed by safeclib).
- Add a small C+ extension for allocation (`malloc_s/calloc_s/realloc_s/free_s`)
  with explicit project error codes; these are **not** Microsoft/Apple CRT APIs.
- Reuse vendored upstream safeclib code (MIT, preserved provenance) instead of
  rewriting checked mem/str primitives.
- Make every claim testable: examples, unit tests, fuzz targets, and a matrix.

## Build

```sh
sh scripts/build_safecrt.sh
# produces build/safe/libcplus_safecrt.a
```

CMake:

```sh
cmake -S stdlib/safe -B build/safe-cmake
cmake --build build/safe-cmake
```

## Public headers

- `safe_memory.h` - `cplus_memcpy_s`, `memmove_s`, `memset_s`, `memcmp_s`, `memzero_s`
- `safe_string.h` - `cplus_strcpy_s`, `strncpy_s`, `strcat_s`, `strncat_s`, `strnlen_s`, `strtok_s`
- `safe_alloc.h`  - C+ extension `malloc_s/calloc_s/realloc_s/free_s`
- `safe_io.h`     - `cplus_fopen_s/fclose_s/fread_s/fwrite_s`
- `safe_format.h` - `cplus_snprintf_s/vsnprintf_s/printf_s/fprintf_s/scanf_s/fscanf_s/sprintf_s/vsprintf_s`
- `safe_error.h`  - error codes and safeclib constraint mode API
- `safe_log.h`    - optional metadata logger (OFF by default)
- `safe_platform.h` - native capability probe

## Constraint handling (safeclib)

`safe_error.h` exposes the constraint-handler mode used by safeclib-backed mem/str
adapters:

- `CPLUS_SAFE_CONSTRAINT_IGNORE` (default): violations return error/defined
  behavior and execution continues.
- `CPLUS_SAFE_CONSTRAINT_ABORT`: violations invoke abort handler; process
  terminates by design.

Use:

```c
#include "safe_error.h"

cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);
cplus_safe_constraint_mode mode = cplus_safe_get_constraint_mode();
```

Invalid mode values fall back to `IGNORE`. The mode is applied to both mem and
str constraint handlers.

## Safety boundary

Runtime checks catch invalid sizes/null/overflow **as specified by the API**.
They do not prove pointer provenance, do not prevent alias UAF, and do not make
unknown-extents safe. The compiler (Agent 1, #21) is responsible for
fail-closed safe-mode enforcement where proof is required.
