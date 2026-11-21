# C+ hardened libc coverage (#22)

This is an honest coverage report. The library is **selectively linked** and **partial**.

## Shipped now (built, tested)

| Family | Symbols | Impl | Status |
|---|---|---|---|
| errno | `__cplus_errno_location`, `errno` macro | real TLS errno | implemented |
| stdlib alloc | `cplus_malloc/calloc/realloc/free` (+ checked `malloc_s/calloc_s/realloc_s/free_s` extension) | real impl over platform allocator backend with overflow checks/log hooks | implemented / selectively linked |
| string/mem | `cplus_memcpy/memmove/memset/memcmp`, `cplus_strlen/strcpy/strncpy/strcat/strncat/strcmp/strncmp` | real C loops | implemented |
| stdio | `cplus_fopen/fclose/fread/fwrite/snprintf/vsnprintf/printf/fprintf/scanf/fscanf` | thin platform-backed wrappers with logging | implemented (platform-backed) |
| ctype | `cplus_isalpha/isdigit/isspace` | real impl | implemented |
| checked Annex-K-style subset | `cplus_memcpy_s` etc. | vendored Intel safestringlib (MIT, pinned) | implemented |

## Not yet shipped (tracked, not claimed)

- Full ISO C stdio FILE implementation (buffering, wide, error flags) — platform-backed wrappers only.
- locale/wchar/multibyte/time/math/signal/threads/full stdlib.
- Full Annex K coverage and platform parity.
- System CRT replacement on macOS/Windows — explicitly out of scope/unsafe.

## Build

```sh
cmake -S libc -B build/libc
cmake --build build/libc
# or
sh tests/libc/build_libc.sh
```
