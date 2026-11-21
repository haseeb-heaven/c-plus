# musl libc provenance

- Upstream: https://git.musl-libc.org/git/musl
- Commit: 7813e9119eb5fe681b6750b8a033a0490bca0941
- License: MIT (see COPYRIGHT)
- Use: selectively copied minimal allocation/string/errno/ctype sources for C+ hardened libc bundle.

## Copied

- COPYRIGHT
- src/malloc/* (malloc.c etc. as present)
- src/string/memcpy.c memmove.c memset.c memcmp.c strlen.c strcpy.c strncpy.c strcat.c strncat.c strcmp.c strncmp.c strchr.c strrchr.c strstr.c memchr.c
- src/errno/__errno_location.c
- src/ctype/isalpha.c isdigit.c isspace.c
- include/errno.h string.h stdlib.h stdio.h ctype.h (reference; C+ headers are in libc/include)

## Adaptations

Document any edits in git history; initial copy preserves upstream content.

## Additional copied (2026-11-22)

- src/string/memccpy.c stpcpy.c stpncpy.c strtok_r.c strsep.c memmem.c strverscmp.c strcasestr.c strchrnul.c
- Integrated as cplus_memccpy/cplus_stpcpy/cplus_stpncpy/cplus_strtok_r/cplus_strsep
  in libc/src/string/musl_extra.c (verbatim bodies renamed; strtok_r uses C+
  strspn/strcspn; logging added). Tests: tests/libc/test_musl_extra.c.

## Additional copied stdlib (2026-11-22)

- src/stdlib/div.c ldiv.c lldiv.c llabs.c atoll.c
- Integrated as cplus_div/ldiv/lldiv/llabs/atoll (verbatim renamed; logging).
  Tests: tests/libc/test_musl_stdlib_extra.c.
