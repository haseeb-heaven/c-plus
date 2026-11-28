/* Plain-hardened C API layer for the standalone C+ libc.
 *
 * This makes ORDINARY ISO C calls safe WITHOUT requiring `_s` spellings and
 * WITHOUT linking the C+ SafeCRT library. It is the libc counterpart of
 * stdlib/safe/safe_c.h (which uses safeclib constraint handlers).
 *
 * Usage:
 *   #include "cplus_libc_safe.h"      // or force-include with -include
 *   link: build/libc/libcplus_hardened.a -lm -pthread
 *
 * Define CPLUS_LIBC_SAFE_DISABLE before the include to get declarations only.
 *
 * Enforcement: tracked allocation metadata for cplus_malloc blocks (including
 * interior pointers and the one-past-end address) plus __builtin_object_size
 * for stack/global objects. Unknown capacity is explicit: warning in default
 * mode, refusal after cplus_libc_set_unknown_capacity_mode(1). This is NOT a
 * proof of pointer lifetime or alias safety; see docs/libc/ORDINARY_C_SAFETY.md.
 */
#ifndef CPLUS_LIBC_SAFE_H
#define CPLUS_LIBC_SAFE_H

#include "cplus_libc.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CPLUS_LIBC_SAFE_DISABLE
#define CPLUS_LIBC_SAFE_ENABLE 1
#endif

#if defined(__GNUC__) || defined(__clang__)
#define CPLUS_LIBC_OBJ_SIZE(p) __builtin_object_size((p), 1)
#else
#define CPLUS_LIBC_OBJ_SIZE(p) ((size_t)-1)
#endif

#ifdef CPLUS_LIBC_SAFE_ENABLE

/* Allocation: overflow checking, failure handling, live metadata. */
#undef malloc
#undef calloc
#undef realloc
#undef free
#define malloc(sz)      cplus_malloc(sz)
#define calloc(n, sz)   cplus_calloc((n), (sz))
#define realloc(p, sz)  cplus_realloc((p), (sz))
#define free(p)         cplus_free(p)

/* Memory and strings: capacity-checked with standard argument lists. */
#undef memset
#undef memcpy
#undef memmove
#undef memcmp
#define memset(d, c, n) \
    ((void)cplus_memset_bounded((d), (c), (n), CPLUS_LIBC_OBJ_SIZE(d)))
#define memcpy(d, s, n) \
    ((void)cplus_memcpy_bounded((d), (s), (n), CPLUS_LIBC_OBJ_SIZE(d)))
#define memmove(d, s, n) \
    ((void)cplus_memmove_bounded((d), (s), (n), CPLUS_LIBC_OBJ_SIZE(d)))
#define memcmp(a, b, n) \
    cplus_memcmp_bounded((a), (b), (n), CPLUS_LIBC_OBJ_SIZE(a), CPLUS_LIBC_OBJ_SIZE(b))

#undef strcpy
#undef strncpy
#undef strcat
#undef strncat
#define strcpy(d, s)    cplus_strcpy_bounded((d), (s), CPLUS_LIBC_OBJ_SIZE(d))
#define strncpy(d, s, n) cplus_strncpy_bounded((d), (s), (n), CPLUS_LIBC_OBJ_SIZE(d))
#define strcat(d, s)    cplus_strcat_bounded((d), (s), CPLUS_LIBC_OBJ_SIZE(d))
#define strncat(d, s, n) cplus_strncat_bounded((d), (s), (n), CPLUS_LIBC_OBJ_SIZE(d))

/* Stream I/O and formatting: overflow guards and compiler format attributes. */
#undef fread
#undef fwrite
#define fread(p, sz, n, f)  cplus_fread((p), (sz), (n), (f))
#define fwrite(p, sz, n, f) cplus_fwrite((p), (sz), (n), (f))
#undef printf
#undef fprintf
#define printf(...)  cplus_printf(__VA_ARGS__)
#define fprintf(...) cplus_fprintf(__VA_ARGS__)

#endif /* CPLUS_LIBC_SAFE_ENABLE */
#endif /* CPLUS_LIBC_SAFE_H */
