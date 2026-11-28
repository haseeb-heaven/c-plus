# P0 regression tests — Safe CRT memory/string + allocation contracts

## Files created (strict ownership: these three only)

| File | Purpose |
| --- | --- |
| `tests/crt/unit/test_p0_safe_crt.c` | P0 memory/string/format regression checks |
| `tests/crt/unit/test_p0_alloc_contract.c` | P0 allocation-contract regression checks |
| `tests/crt/unit/P0_TESTS_NOTES.md` | This file |

No headers, build scripts, existing tests, or other repository files were
edited. No `git add` / `git commit` performed. Build outputs live under the
already-gitignored `build/` directory.

## Toolchain / environment

- Compiler: `/usr/bin/clang`, Apple clang version 16.0.0 (clang-1600.0.26.6),
  target arm64-apple-darwin27.2.0.
- Language: `-std=c99 -Wall -Wextra -pedantic` (zero warnings in all modes).
- Libraries linked (existing artifacts, unchanged by this work):
  - `build/safe/libcplus_safecrt.a`
  - `build/libc/libcplus_hardened.a`
  - plus `-lm -pthread`.

Include flags used in every compile:

```
-I. -Istdlib/safe -Ithird_party/safeclib/include -Ilibc -Ilibc/install-include
```

## APIs used (all confirmed declared in headers before use)

- `stdlib/safe/safe_memory.h`: `cplus_memcpy_s`, `cplus_memmove_s`,
  `cplus_memcmp_s`.
- `stdlib/safe/safe_string.h`: `cplus_strcpy_s`, `cplus_strncpy_s`,
  `cplus_strcat_s`, `cplus_strncat_s`, `cplus_strnlen_s`.
- `stdlib/safe/safe_format.h`: `cplus_snprintf_s`, `cplus_vsnprintf_s`.
- `stdlib/safe/safe_alloc.h`: `malloc_s`, `calloc_s`, `realloc_s`, `free_s`,
  `free_checked_s`, `cplus_safe_alloc_get_meta`, `cplus_safe_ptr_is_live`,
  `cplus_safe_ptr_size`, `cplus_safe_ptr_elem_size`, `cplus_safe_ptr_count`,
  `cplus_safe_query_length`, `cplus_safe_ptr_check_generation`,
  `cplus_safe_ptr_was_freed`, `cplus_safe_len`, `cplus_safe_alloc_live_count`,
  `cplus_safe_alloc_live_bytes`, `cplus_safe_alloc_fail_after`,
  `cplus_safe_alloc_fail_reset`, `cplus_safe_alloc_seq`,
  `cplus_safe_transfer_ownership`.
- `stdlib/safe/safe_error.h` / `safe_log.h`: constraint mode, `RSIZE_MAX_*`,
  `CPLUS_SAFE_OK` / `CPLUS_SAFE_EINVAL`, log level.
- `libc/install-include/cplus_libc.h`: `cplus_strlen`, `cplus_malloc`,
  `cplus_free_checked`, `cplus_alloc_is_live` (for the pointer-to-pointer
  cleanup path; this symbol set lives in the hardened libc, which the task
  required linking).

Safeclib constraint codes are re-declared locally under `*_C` names
(`ESNULLP 400`, `ESZEROL 401`, `ESLEMAX 403`, `ESOVRLP 404`, `ESNOSPC 406`);
they were verified against `third_party/safeclib/include/safe_lib_errno.h`.
`EOVERFLOW 75`, `ENOMEM 12`, `EINVAL 22` come from `errno.h` / `safe_error.h`.

## Exact verification commands and results

From the repository root. Each build was run before its binary; all three
modes were re-run against the final libraries.

### test_p0_safe_crt.c — normal

```
/usr/bin/clang -std=c99 -Wall -Wextra -pedantic \
  -I. -Istdlib/safe -Ithird_party/safeclib/include -Ilibc -Ilibc/install-include \
  tests/crt/unit/test_p0_safe_crt.c \
  build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
  -lm -pthread -o build/safe/test_p0_safe_crt_final_normal
build/safe/test_p0_safe_crt_final_normal
```

Result: build 0 warnings/errors, run exit `0`, `P0_SAFE_CRT 138 0`,
138 tag lines matching `^[a-z_0-9]+ [01]$`, 0 failing tags, 0 stderr bytes.

### test_p0_safe_crt.c — UBSan

```
/usr/bin/clang -std=c99 -Wall -Wextra -pedantic \
  -fsanitize=undefined -fno-sanitize-recover=all \
  -I. -Istdlib/safe -Ithird_party/safeclib/include -Ilibc -Ilibc/install-include \
  tests/crt/unit/test_p0_safe_crt.c \
  build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
  -lm -pthread -o build/safe/test_p0_safe_crt_final_ubsan
build/safe/test_p0_safe_crt_final_ubsan
```

Result: build 0 warnings/errors, run exit `0`, `P0_SAFE_CRT 138 0`,
138 matching tag lines, 0 failing tags, **empty UBSan stderr** (no runtime
undefined-behaviour reports in the test code).

### test_p0_safe_crt.c — optimized (harness-equivalent `-O2`)

```
/usr/bin/clang -O2 -std=c99 -Wall -Wextra -pedantic \
  -I. -Istdlib/safe -Ithird_party/safeclib/include -Ilibc -Ilibc/install-include \
  tests/crt/unit/test_p0_safe_crt.c \
  build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
  -lm -pthread -o build/safe/test_p0_safe_crt_final_o2
build/safe/test_p0_safe_crt_final_o2
```

Result: build 0 warnings/errors, run exit `0`, `P0_SAFE_CRT 138 0`,
0 failing tags.

### test_p0_alloc_contract.c — normal

```
/usr/bin/clang -std=c99 -Wall -Wextra -pedantic \
  -I. -Istdlib/safe -Ithird_party/safeclib/include -Ilibc -Ilibc/install-include \
  tests/crt/unit/test_p0_alloc_contract.c \
  build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
  -lm -pthread -o build/safe/test_p0_alloc_contract_final_normal
build/safe/test_p0_alloc_contract_final_normal
```

Result: build 0 warnings/errors, run exit `0`, `P0_ALLOC 85 0`,
85 tag lines, 0 failing tags, 0 stderr bytes.

### test_p0_alloc_contract.c — UBSan

```
/usr/bin/clang -std=c99 -Wall -Wextra -pedantic \
  -fsanitize=undefined -fno-sanitize-recover=all \
  -I. -Istdlib/safe -Ithird_party/safeclib/include -Ilibc -Ilibc/install-include \
  tests/crt/unit/test_p0_alloc_contract.c \
  build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
  -lm -pthread -o build/safe/test_p0_alloc_contract_final_ubsan
build/safe/test_p0_alloc_contract_final_ubsan
```

Result: build 0 warnings/errors, run exit `0`, `P0_ALLOC 85 0`,
85 matching tag lines, 0 failing tags, **empty UBSan stderr**.

### test_p0_alloc_contract.c — optimized (harness-equivalent `-O2`)

```
/usr/bin/clang -O2 -std=c99 -Wall -Wextra -pedantic \
  -I. -Istdlib/safe -Ithird_party/safeclib/include -Ilibc -Ilibc/install-include \
  tests/crt/unit/test_p0_alloc_contract.c \
  build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
  -lm -pthread -o build/safe/test_p0_alloc_contract_final_o2
build/safe/test_p0_alloc_contract_final_o2
```

Result: build 0 warnings/errors, run exit `0`, `P0_ALLOC 85 0`, 0 failing tags.

### Summary table

| Test | Normal | UBSan | `-O2` |
| --- | --- | --- | --- |
| `test_p0_safe_crt.c` | 138 / 0, exit 0 | 138 / 0, exit 0, clean stderr | 138 / 0, exit 0 |
| `test_p0_alloc_contract.c` | 85 / 0, exit 0 | 85 / 0, exit 0, clean stderr | 85 / 0, exit 0 |

(`checks / failures`)

## What each test covers

### test_p0_safe_crt.c (138 checks)

- **memcpy_s / memmove_s**: forward overlap (dest above src) and backward
  overlap (dest below src), overlap with `smax < dmax`, identical-pointer
  no-op, distinct-object copy, `smax > dmax`, NULL src/dest, zero-length with
  NULL pointers, `dmax == 0`, `dmax`/`slen` above `RSIZE_MAX_MEM`. Guard bytes
  past `dmax` prove no out-of-bounds write on every refusal.
- **memcmp_s / strlen / strnlen**: equal/less/greater ordering, unsigned byte
  order, prefix-only comparison, NULL diff/dest/src with `*diff` reset to -1,
  zero `dmax`, `smax > dmax`, over-limit sizes; `cplus_strlen` on empty,
  terminated and embedded-NUL strings; `cplus_strnlen_s` windows at 0/1/
  exact/over-length, NULL, unterminated windows, equality with `strlen`, and
  read-only guard bytes.
- **strcpy_s / strncpy_s / strcat_s / strncat_s**: exact fit, truncation via
  explicit `slen`, empty source, over-capacity refusal, NULL arguments,
  `dmax == 0`, over-limit `dmax`, self-copy, reuse of an object after a
  refusal, and guard bytes around a struct sub-object.
- **vsnprintf truncation** (the gap in existing coverage): would-be length on
  truncation, bounded NUL, byte at index `dmax` untouched, `dmax == 1`,
  `dmax == 0` query form, NULL-dest query vs NULL-dest refusal, numeric
  truncation, and one `vsnprintf`/`snprintf` agreement check.

### test_p0_alloc_contract.c (85 checks)

- `calloc_s(8,16)`: 128 zeroed bytes and metadata (`byte_size 128`,
  `elem_size 16`, `elem_count 8`, `is_live`, `generation > 0`,
  `query_length`, `cplus_safe_len`) plus end-to-end writeability.
- `realloc_s`: failure injection returns NULL/`ENOMEM` while preserving the
  original block's live status, size, bytes and generation; success returns a
  new block with bumped generation, invalidates the old pointer, preserves
  bytes; `realloc_s(NULL, n)` allocates fresh.
- `free_s` / `free_checked_s`: NULL no-op, valid free, `was_freed`, double
  free and invalid frees (stack, interior, raw `malloc` memory) refused with
  `EINVAL` without disturbing the live table; raw block released with raw
  `free`.
- Zero-size: `malloc_s(0)`, `calloc_s(0,0)`, `calloc_s(4,0)` all yield a
  defined tracked 1-byte block.
- Overflow/limits: `calloc_s(SIZE_MAX,2)` and `calloc_s(SIZE_MAX/2+1,2)` →
  `EOVERFLOW`; `calloc_s(RSIZE_MAX_MEM,1)` → `ENOMEM`; `malloc_s((size_t)-1)`
  → `EINVAL`.
- Failure injection: `fail_after(1)` lets one allocation succeed then refuses
  with `ENOMEM`; `fail_reset()` restores allocation; sequence counter never
  rewinds.
- Pointer-to-pointer cleanup: `cplus_safe_transfer_ownership` nulls `*src`,
  hands the block to `*dst`, bumps generation, and refuses same-slot / NULL /
  occupied-destination transfers without changing either slot;
  `cplus_free_checked(&ptr)` frees and nulls the slot and is idempotent on
  an already-NULL slot. Final live count and byte total return to baseline.

## Format tests reviewed first (to avoid duplication)

Read before writing the format portion: `test_sprintf_truncation_contract.c`,
`test_allocfmt_format_bounded.c`, `test_snprintf_s.c`, `test_vsnprintf_s.c`.
They already exhaustively sweep `cplus_snprintf_s` / `cplus_sprintf_s`
truncation (all `dmax` 1..16, exact fit, `dmax` 0/1, NULL query forms, guard
bytes). This file therefore adds only the `cplus_vsnprintf_s` va_list path
and a single snprintf cross-check, not a duplicate sweep.

## Contract interpretation notes (not defects)

- **"Destination unchanged on refusal"**: safeclib's documented and tested
  contract is that a constraint violation never writes beyond `dmax`, and
  puts the in-capacity prefix into a defined safe state (memory adapters
  zero the first `dmax` bytes; string adapters reset `dest[0]` to `'\0'`).
  Existing tests (`test_strfam_strcpy_s.c`, `test_strcpy_strncat_overflow.c`,
  `test_memfam_overlap.c`) assert the same. Accordingly, the P0 tests assert
  (a) bytes beyond `dmax` / guard bytes are unchanged on refusal and (b) the
  in-capacity prefix is the documented safe state. They deliberately do not
  assert byte-for-byte preservation of the whole destination, which would
  contradict the library's contract.
- `strncat_s(..., slen == 0)` is refused with `ESZEROL` (401) and resets the
  destination to empty; `strncpy_s(..., slen == 0)` succeeds with an empty
  result. These asymmetries were verified against the implementation and are
  intentional safeclib behaviour.
- `memcpy_s`/`memmove_s` with `slen == 0` return `EOK` before any pointer
  check, so NULL pointers are accepted for a zero-length operation.
- The safeclib ignore handler prints lines like
  `IGNORE CONSTRAINT HANDLER: (406) ...` to **stdout** when a violation is
  exercised (this is compiled into the prebuilt `libcplus_safecrt.a` via
  `sldebug_printf`; `cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF)` does not
  silence it). This is pre-existing library diagnostic output, not a test
  failure. The P0 tag lines remain machine-parseable: they are the only
  stdout lines matching `^[a-z_0-9]+ [01]$`, and the final summary is the
  only line starting with `P0_SAFE_CRT` / `P0_ALLOC`.

## Defects found

**None.** All 223 checks (138 + 85) pass in normal, UBSan and `-O2` modes with
exit status 0, empty UBSan stderr, and zero compiler warnings. No library
header was changed. The stdout handler noise noted above is an observability
nit in the existing prebuilt library, not a correctness defect, and is left
unchanged per the no-header-edit / no-other-file constraints.
