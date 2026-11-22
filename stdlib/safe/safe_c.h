/* C+ hardened standard-C API layer (ordinary, unchanged C spelling).
 *
 * PURPOSE
 * -------
 * Make ordinary ISO C calls safer WITHOUT requiring callers to rewrite them
 * to `_s` names. Include this header first (or force-include it with
 * `-include stdlib/safe/safe_c.h`), then compile/link against
 * `build/safe/libcplus_safecrt.a` and `build/libc/libcplus_hardened.a`.
 *
 *   #include "stdlib/safe/safe_c.h"
 *   unsigned char *p = malloc(64);     // -> tracked malloc_s
 *   memset(p + 60, 0x41, 8);           // -> capacity-checked wrapper, refused
 *
 * The macros are enabled on inclusion. Define `CPLUS_SAFE_C_DISABLE` before
 * the include to get pure declarations only.
 *
 * WHAT IS ACTUALLY ENFORCED
 * -------------------------
 *  - malloc/calloc/realloc/free: overflow-checked, failure-checked, tracked in
 *    a live-allocation table. Interior pointers resolve to the REMAINING
 *    capacity of their containing block, so `buffer + offset` is checked too.
 *  - memset/memcpy/memmove/memcmp/strcpy/strncpy/strcat/strncat: the auto
 *    wrappers combine __builtin_object_size(p,1) for stack/global objects with
 *    tracked remaining capacity for heap blocks. A write larger than the
 *    proven capacity is refused with a constraint violation (ESNOSPC 406)
 *    instead of corrupting memory.
 *  - fread/fwrite: the (size * count) product is checked against SIZE_MAX.
 *  - printf/fprintf: mapped to C+ wrappers carrying Clang `format` attributes
 *    so format/argument mismatches are diagnosed at compile time.
 *
 * HONEST LIMITS
 * -------------
 *  - When capacity is genuinely unknown, default mode preserves raw-C behaviour
 *    but emits a warning; it does NOT claim the operation is safe. Use
 *    `cplus_safe_set_unknown_capacity_mode(1)` for strict fail-closed mode.
 *  - Overlapping raw memcpy is still UB unless the checked wrapper detects it;
 *    use memmove for overlap.
 *  - Raw `printf` with an untrusted format remains unsafe. The checked `_s`
 *    format entry points (stdlib/safe/safe_format.h) reject non-literal
 *    formats and `%n`; use those for untrusted input.
 *  - This is runtime diagnosis, not a proof of pointer provenance, lifetime or
 *    alias safety. See docs/libc/C_BUG_CLASS_MATRIX.md.
 */
#ifndef CPLUS_SAFE_C_H
#define CPLUS_SAFE_C_H

#include "safe_alloc.h"
#include "safe_memory.h"
#include "safe_string.h"
#include "safe_io.h"
#include "safe_format.h"
/* cplus_fread/cplus_fwrite/cplus_printf/cplus_fprintf carry the overflow
 * guards and Clang format attributes used by the mappings below. */
#include "cplus_libc.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(CPLUS_SAFE_C_DISABLE)
#define CPLUS_SAFE_C_ENABLE 1
#endif

#ifdef CPLUS_SAFE_C_ENABLE

/* ---- allocation: overflow-checked and tracked -------------------------- */
#undef malloc
#undef calloc
#undef realloc
#undef free
#define malloc(sz)          malloc_s(sz)
#define calloc(n, sz)       calloc_s((n), (sz))
#define realloc(p, sz)      realloc_s((p), (sz))
#define free(p)             free_s(p)

/* ---- memory: capacity-checked with standard argument lists ------------- */
#undef memset
#undef memcpy
#undef memmove
#undef memcmp
#define memset(d, c, n) \
    ((void)cplus_memset_s_auto((d), (c), (n), CPLUS_SAFE_OBJ_SIZE(d)))
#define memcpy(d, s, n) \
    ((void)cplus_memcpy_s_auto((d), (s), (n), CPLUS_SAFE_OBJ_SIZE(d)))
#define memmove(d, s, n) \
    ((void)cplus_memmove_s_auto((d), (s), (n), CPLUS_SAFE_OBJ_SIZE(d)))
#define memcmp(a, b, n) \
    cplus_memcmp_s_auto((a), (b), (n), CPLUS_SAFE_OBJ_SIZE(a), CPLUS_SAFE_OBJ_SIZE(b))

/* ---- strings: bounded against proven destination capacity -------------- */
#undef strcpy
#undef strncpy
#undef strcat
#undef strncat
#define strcpy(d, s) \
    cplus_strcpy_s_auto((d), (s), CPLUS_SAFE_OBJ_SIZE(d))
#define strncpy(d, s, n) \
    cplus_strncpy_s_auto((d), (s), (n), CPLUS_SAFE_OBJ_SIZE(d))
#define strcat(d, s) \
    cplus_strcat_s_auto((d), (s), CPLUS_SAFE_OBJ_SIZE(d))
#define strncat(d, s, n) \
    cplus_strncat_s_auto((d), (s), (n), CPLUS_SAFE_OBJ_SIZE(d))

/* ---- stream I/O: multiplication overflow + NULL guards ----------------- */
#undef fread
#undef fwrite
#define fread(p, sz, n, f)  cplus_fread((p), (sz), (n), (f))
#define fwrite(p, sz, n, f) cplus_fwrite((p), (sz), (n), (f))

/* ---- formatted output: compiler format checking via attributed wrappers - */
#undef printf
#undef fprintf
#define printf(...)  cplus_printf(__VA_ARGS__)
#define fprintf(...) cplus_fprintf(__VA_ARGS__)

#endif /* CPLUS_SAFE_C_ENABLE */
#endif /* CPLUS_SAFE_C_H */
