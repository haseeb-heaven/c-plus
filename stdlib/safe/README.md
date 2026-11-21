# `stdlib/safe` — optional checked C standard-library layer

This directory provides **optional** checked APIs alongside the unchanged C
standard library. It does **not** replace libc, does **not** rename existing
libc symbols, and does **not** claim that ordinary C becomes memory-safe.

## Goals

- Preserve familiar C names/contracts where platform `_s` APIs exist (`fopen_s`,
  `sprintf_s`, Annex K-style mem/str functions via Intel safestringlib).
- Add a small C+ extension for allocation (`malloc_s/calloc_s/realloc_s/free_s`)
  with explicit project error codes; these are **not** Microsoft/Apple CRT APIs.
- Reuse existing licensed upstream code (Intel safestringlib) instead of
  rewriting checked mem/str primitives.
- Make every claim testable: examples, unit tests, fuzz targets, and a matrix.

## Build

```sh
sh scripts/build_safecrt.sh
# produces build/safe/libcplus_safecrt.a
```

CMake:

```sh
cmake -S third_party/intel-safestringlib -B build/safe-cmake
cmake --build build/safe-cmake
```

## Public headers

- `safe_memory.h` - `cplus_memcpy_s`, `memmove_s`, `memset_s`, `memcmp_s`, `memzero_s`
- `safe_string.h` - `cplus_strcpy_s`, `strncpy_s`, `strcat_s`, `strncat_s`, `strnlen_s`
- `safe_alloc.h`  - C+ extension `malloc_s/calloc_s/realloc_s/free_s`
- `safe_io.h`     - `cplus_fopen_s/fclose_s/fread_s/fwrite_s`
- `safe_format.h` - `cplus_snprintf_s/vsnprintf_s/printf_s/fprintf_s/scanf_s/fscanf_s`
- `safe_log.h`    - optional metadata logger (OFF by default)
- `safe_platform.h` - native capability probe

## Safety boundary

Runtime checks catch invalid sizes/null/overflow **as specified by the API**.
They do not prove pointer provenance, do not prevent alias UAF, and do not make
unknown-extents safe. The compiler (Agent 1, #21) is responsible for
fail-closed safe-mode enforcement where proof is required.
