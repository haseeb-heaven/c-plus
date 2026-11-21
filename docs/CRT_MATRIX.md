# CRT matrix (Agent 2 / #22)

Evidence-based matrix for shipped scope. No full ISO C claim.

| Function | Class | Header | Impl | Example | Unit | Fuzz | Notes |
|---|---|---|---|---|---|---|---|
| cplus_malloc | IMPLEMENTED | libc/install-include/cplus_libc.h | libc/src/stdlib/alloc.c | n/a (libc) | tests/libc/test_libc_basic.c | no | platform allocator backend, overflow checks |
| cplus_calloc | IMPLEMENTED | cplus_libc.h | alloc.c | n/a | libc basic | no | overflow checked |
| cplus_realloc | IMPLEMENTED | cplus_libc.h | alloc.c | n/a | libc basic | no | preserves old on failure by backend contract |
| cplus_free | IMPLEMENTED | cplus_libc.h | alloc.c | n/a | libc basic | no | |
| cplus_memcpy | IMPLEMENTED | cplus_libc.h | libc/src/string/memory.c | n/a | libc basic | no | not bounds-safe for unknown raw pointers |
| cplus_memmove | IMPLEMENTED | cplus_libc.h | memory.c | n/a | libc basic | no | overlap-safe |
| cplus_memset | IMPLEMENTED | cplus_libc.h | memory.c | n/a | libc basic | no | |
| cplus_memcmp | IMPLEMENTED | cplus_libc.h | memory.c | n/a | libc basic | no | |
| cplus_strlen/strcpy/strncpy/strcat/strncat/strcmp/strncmp | IMPLEMENTED | cplus_libc.h | libc/src/string/string.c | n/a | libc basic | no | raw C bounds still need compiler proof |
| cplus_memcpy_s | CHECKED-OPTIONAL | stdlib/safe/safe_memory.h | Intel safestringlib | examples/crt/memcpy_s.c | tests/crt/unit/test_memcpy_s.c | fuzz_memory.c smoke | pinned MIT upstream |
| cplus_memmove_s | CHECKED-OPTIONAL | safe_memory.h | Intel safestringlib | examples/crt/memmove_s.c | unit | fuzz_memory.c smoke | |
| cplus_memset_s | CHECKED-OPTIONAL | safe_memory.h | Intel safestringlib | examples/crt/memset_s.c | unit | fuzz_memory.c smoke | |
| cplus_memcmp_s | CHECKED-OPTIONAL | safe_memory.h | Intel safestringlib | examples/crt/memcmp_s.c | unit | fuzz_memory.c smoke | |
| cplus_memzero_s | CHECKED-OPTIONAL | safe_memory.h | Intel safestringlib | examples/crt/memzero_s.c | unit | fuzz_memory.c smoke | |
| cplus_strcpy_s | CHECKED-OPTIONAL | stdlib/safe/safe_string.h | Intel safestringlib | examples/crt/strcpy_s.c | unit | fuzz_strings.c smoke | |
| cplus_strncpy_s | CHECKED-OPTIONAL | safe_string.h | Intel safestringlib | examples/crt/strncpy_s.c | unit | fuzz_strings.c smoke | |
| cplus_strcat_s | CHECKED-OPTIONAL | safe_string.h | Intel safestringlib | examples/crt/strcat_s.c | unit | fuzz_strings.c smoke | |
| cplus_strncat_s | CHECKED-OPTIONAL | safe_string.h | Intel safestringlib | examples/crt/strncat_s.c | unit | fuzz_strings.c smoke | |
| cplus_strcmp_s | CHECKED-OPTIONAL | safe_string.h | Intel safestringlib | examples/crt/strcmp_s.c | unit | fuzz_strings.c smoke | |
| cplus_strnlen_s | CHECKED-OPTIONAL | safe_string.h | Intel safestringlib | examples/crt/strnlen_s.c | unit | fuzz_strings.c smoke | |
| malloc_s/calloc_s/realloc_s/free_s | C+ EXTENSION (not MS/Apple CRT) | stdlib/safe/safe_alloc.h | stdlib/safe/src/safe_alloc.c | examples/crt/*alloc*_s.c | unit | fuzz_allocator.c smoke | explicit error codes |
| cplus_fopen_s/fclose_s | CHECKED-OPTIONAL adapter | stdlib/safe/safe_io.h | stdlib/safe/src/safe_io.c | examples/crt/fopen_s.c fclose_s.c | unit | fuzz_stdio.c smoke | platform-backed |
| cplus_fread_s/fwrite_s | CHECKED-OPTIONAL adapter | safe_io.h | safe_io.c | examples | unit | fuzz_stdio.c smoke | bounds-checked count |
| cplus_snprintf_s/vsnprintf_s/sprintf_s/vsprintf_s | CHECKED-OPTIONAL adapter | stdlib/safe/safe_format.h | stdlib/safe/src/safe_format.c | examples | unit | fuzz_format.c smoke | bounded; not full Annex K parity |
| cplus_printf_s/fprintf_s/scanf_s/fscanf_s | adapter | safe_format.h | safe_format.c | examples | unit | no | platform-backed; scanf unsafe with unbounded %s |

## Verification commands (executed on this host)

```sh
sh tests/crt/run_all_crt.sh          # CRT corpus 10/0, safecrt 6/0, examples 26/0, fuzz smoke 4/0 (legacy script) 
sh tests/crt/unit/run_unit_tests.sh  # 26/26
sh tests/crt/fuzz/run_fuzz_smoke.sh  # 5/5
sh tests/libc/run_libc_tests.sh      # 1/1
```

## Explicit gaps

- Not full ISO C coverage.
- No host CRT replacement.
- libFuzzer/ASan/UBSan real runs need Linux CI.
- Raw C unknown-capacity/provenance/UAF cannot be fully fixed by libc alone.
