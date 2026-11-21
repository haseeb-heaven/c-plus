# C+ hardened libc coverage (honest, #22/#23)

This document tracks what is actually shipped in `agent/modern-stdlib` (PR #17).
No full ISO C replacement is claimed.

## Status legend

- **IMPLEMENTED**: real compiled source in this repo, tests executed.
- **PLATFORM-BACKED**: C+ symbol exists but delegates to host libc/platform for defined behavior.
- **CHECKED-OPTIONAL**: optional `_s`/checked API, not a standard libc replacement.
- **UNSUPPORTED/NOT SHIPPED**: not implemented; do not claim.

## Shipped now

| Family | Symbol | Status | Evidence |
|---|---|---|---|
| errno | `__cplus_errno_location`, `errno` header macro | IMPLEMENTED | `tests/libc/test_libc_basic.c`, `tests/libc/run_libc_tests.sh` |
| alloc | `cplus_malloc/calloc/realloc/free` | IMPLEMENTED (platform allocator backend + overflow checks/log hooks) | `tests/libc/run_libc_tests.sh` |
| memory | `cplus_memcpy/memmove/memset/memcmp` | IMPLEMENTED | `tests/libc/run_libc_tests.sh` |
| string | `cplus_strlen/strcpy/strncpy/strcat/strncat/strcmp/strncmp` | IMPLEMENTED | `tests/libc/run_libc_tests.sh` |
| ctype | `cplus_isalpha/isdigit/isspace` | IMPLEMENTED (ASCII/basic) | `libc/src/ctype/ctype.c` (build) |
| stdio | `cplus_fopen/fclose/fread/fwrite/snprintf/vsnprintf/printf/fprintf/scanf/fscanf` | PLATFORM-BACKED | build/test smoke via libc build; full FILE impl not shipped |
| checked memory | `cplus_memcpy_s/memmove_s/memset_s/memcmp_s/memzero_s` | CHECKED-OPTIONAL (Intel safestringlib MIT, pinned) | `tests/crt/unit/*mem*_s.c`, `tests/crt/run_safecrt_tests.sh` |
| checked string | `cplus_strcpy_s/strncpy_s/strcat_s/strncat_s/strcmp_s/strnlen_s` | CHECKED-OPTIONAL | `tests/crt/unit/*str*_s.c` |
| checked alloc ext | `malloc_s/calloc_s/realloc_s/free_s` | CHECKED-OPTIONAL C+ EXTENSION (not MS/Apple CRT) | `tests/crt/unit/test_*alloc*_s.c` |
| checked io | `cplus_fopen_s/fclose_s/fread_s/fwrite_s` | CHECKED-OPTIONAL PLATFORM-BACKED | `tests/crt/unit/test_f*_s.c` |
| checked format | `cplus_snprintf_s/vsnprintf_s/sprintf_s/vsprintf_s/printf_s/fprintf_s/scanf_s/fscanf_s` | CHECKED-OPTIONAL PLATFORM-BACKED adapter | `tests/crt/unit/test_*printf*_s.c`, `test_*scanf*_s.c` |

## Not shipped (explicit)

Full ISO C: complete stdio FILE buffering, wide char, locale, full ctype/locale tables, math, time, signal, setjmp integration, threads/atomics full API, complex, fenv full platform parity, full Annex K parity. Host CRT replacement on macOS/Windows is **not** supported.

## Upstream provenance

- Intel safestringlib: `third_party/intel-safestringlib/`, commit `f4491915cc682556ed64d4eb6a2461d8eed2eb0c`, MIT, symbol-renamed.
- musl reference files copied for provenance/reference in `libc/upstream/musl/` (not all compiled as libc replacement). See `libc/upstream/musl/PROVENANCE.md`.
