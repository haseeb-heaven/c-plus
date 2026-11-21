# C+ hardened libc coverage (honest, #22/#23)

Tracks what is actually shipped/verified on `agent/modern-stdlib` (PR #17). **No full ISO C replacement claim.**

## Verified (this host)

| Area | Command | Result |
|---|---|---|
| CRT corpus | `sh tests/crt/run_crt_tests.sh` | 10 passed, 0 failed |
| Safe CRT unit | `sh tests/crt/unit/run_unit_tests.sh` | 26 passed, 0 failed |
| Safe CRT examples | `sh tests/crt/run_all_crt.sh` (examples stage) | 26 passed, 0 failed |
| Fuzz smoke | `sh tests/crt/fuzz/run_fuzz_smoke.sh` | 5 passed, 0 failed |
| Libc tests | `sh tests/libc/run_libc_tests.sh` | 9 passed, 0 failed |
| Libc install smoke | `sh tests/libc/install_smoke.sh` | ok |
| Libc coverage | `sh tests/libc/coverage_libc.sh` | all modules >=80% line except none; stdio 80%, stdio2 91.67%, others 97-100% |

## Shipped libc slice (`libc/`)

| Family | Symbols | Status |
|---|---|---|
| errno | `__cplus_errno_location` | IMPLEMENTED |
| alloc | `cplus_malloc/calloc/realloc/free` | IMPLEMENTED (platform allocator backend + checks/logs) |
| memory | `cplus_memcpy/memmove/memset/memcmp/memchr/memrchr` | IMPLEMENTED |
| string | `cplus_strlen/strnlen/strcpy/strncpy/strcat/strncat/strcmp/strncmp/strchr/strrchr/strstr/strcasecmp/strncasecmp/strdup` | IMPLEMENTED |
| ctype | full basic ctype + toupper/tolower | IMPLEMENTED (ASCII/basic) |
| stdlib | `abs/labs/atoi/atol/strtol/strtoul/qsort/bsearch` | IMPLEMENTED/PLATFORM-BACKED |
| stdio | `fopen/fclose/fread/fwrite/fseek/ftell/fflush/remove/rename/fgets/fputs/getc/putc/feof/ferror/clearerr/snprintf/vsnprintf/printf/fprintf/scanf/fscanf` | PLATFORM-BACKED wrappers, logged |

## Shipped checked CRT (`stdlib/safe/`)

Intel safestringlib-backed (MIT, pinned `f4491915cc682556ed64d4eb6a2461d8eed2eb0c`, renamed symbols): `cplus_memcpy_s/memmove_s/memset_s/memcmp_s/memzero_s`, `cplus_strcpy_s/strncpy_s/strcat_s/strncat_s/strcmp_s/strnlen_s`. Platform-backed adapters: `cplus_fopen_s/fclose_s/fread_s/fwrite_s`, `cplus_snprintf_s/vsnprintf_s/sprintf_s/vsprintf_s/printf_s/fprintf_s/scanf_s/fscanf_s`. C+ extension (not MS/Apple CRT): `malloc_s/calloc_s/realloc_s/free_s`.

## Not shipped / blockers

- Full ISO C: complete FILE buffering, wide char, locale, full math, time, signal, threads/atomics, fenv, complex, full format/scanf parity.
- Real libFuzzer/ASan/UBSan CI on Linux runners (workflow added; remote result unverified here).
- Cross-platform ABI/SDK matrix and Agent 1 contract enforcement interop.
- Raw C UB/provenance/unknown-capacity cannot be made safe by libc alone.
