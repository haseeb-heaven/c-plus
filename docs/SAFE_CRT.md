# Safe CRT layer (#22)

Status: **in-progress evidence, not a safety claim**. This layer adds optional
checked APIs and contract tests alongside unchanged libc. It does not replace
libc, does not add mandatory `*_s` allocation to standard C, and does not make
arbitrary raw C memory-safe.

## What is implemented

| Area | API | Implementation | Notes |
|------|-----|----------------|-------|
| Memory | `cplus_memcpy_s`, `memmove_s`, `memset_s`, `memcmp_s`, `memzero_s` | Intel safestringlib (vendored, MIT) renamed symbols + thin wrapper | Preserves upstream constraint semantics; no libc symbol replacement |
| String | `cplus_strcpy_s`, `strncpy_s`, `strcat_s`, `strncat_s`, `strnlen_s` | Intel safestringlib | `strcpy_s` on too-small dest zeros dest[0] per upstream behavior |
| Allocation extension | `malloc_s`, `calloc_s`, `realloc_s`, `free_s` | C+ extension in `stdlib/safe/src/safe_alloc.c` | **Not** Microsoft/Apple CRT. Returns CPLUS_SAFE_* codes; `realloc_s` preserves owner on failure; `free_s` nulls owner pointer only |
| I/O | `cplus_fopen_s`, `fclose_s`, `fread_s`, `fwrite_s` | Thin wrappers over libc | Preserves errno/FILE* semantics |
| Format/scan | `cplus_snprintf_s`, `vsnprintf_s`, `printf_s`, `fprintf_s`, `scanf_s`, `fscanf_s` | Thin wrappers over libc stdio/varargs | No format-parser rewrite; runtime checks are argument validation only |
| Platform probe | `cplus_safe_platform_probe` | Compile-time feature macros | Evidence, not enforcement |
| Logging | `cplus_safe_log*` | optional metadata logger | OFF by default; no buffer contents |

## Upstream

- Intel safestringlib, commit `f4491915cc682556ed64d4eb6a2461d8eed2eb0c`, MIT.
  Vendored subset in `third_party/intel-safestringlib/` with `PROVENANCE.md`.
  Build-time symbol renaming via `rename_symbols.h` avoids CRT symbol collisions.
- rurban/safeclib cloned for reference (`90b076e94c4b0fb202dddf948d5d620e5395c1b0`) but not linked into binaries.

## Boundaries / not claimed

- `free_s` nulls the passed owner location; it cannot invalidate aliases.
- Bounds checks are runtime/API-contract checks; unknown-extents and alias UAF
  require Agent 1 compiler proof or explicit unsafe boundary.
- `scanf_s` family here wraps libc scanf; Annex K argument-constraint differences
  are platform-dependent and documented in the matrix.
- No unlicensed Apple/Microsoft CRT source copied.
