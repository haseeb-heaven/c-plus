/* PR31 libc+ cross-module fixtures -- shared C ABI declarations.
 *
 * TU A (tu_a.c) is the "producer": it allocates and fills records.
 * TU B (tu_b.c) and driver.c are "consumers": they release, validate and call
 * back across the module boundary.  The consumer deliberately does NOT include
 * cplus_libc.h: only the exported C symbols and the byte layout cross the TU
 * boundary, which is what this fixture is meant to exercise.
 *
 * Declarations are repeated here on purpose.  Including the public header on
 * the consumer side would hide a broken symbol name behind a macro.
 */
#ifndef PR31_SHARED_H
#define PR31_SHARED_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#ifdef PR31_USE_CPLUS_HEADER
/* Producer side: use the real public header so the side table's layout comes
 * from the shipped ABI, not from a copy in this file. */
#include "cplus_libc.h"
#else

typedef struct cplus_alloc_meta {
    size_t byte_size;
    size_t elem_size;
    size_t elem_count;
    unsigned long long generation;
    int is_live;
} cplus_alloc_meta;

extern void *cplus_malloc(size_t);
extern void *cplus_calloc(size_t, size_t);
extern void *cplus_realloc(void *, size_t);
extern void cplus_free(void *);
extern int cplus_free_checked(void **);
extern int cplus_alloc_checked_free(void **);
extern int cplus_alloc_atomic_realloc(void **, size_t);
extern int cplus_alloc_is_live(const void *);
extern size_t cplus_alloc_size(const void *);
extern size_t cplus_alloc_elem_size(const void *);
extern size_t cplus_alloc_length(const void *);
extern int cplus_alloc_query_length(const void *, size_t, size_t *);
extern int cplus_alloc_get_meta(const void *, cplus_alloc_meta *);
extern size_t cplus_alloc_remaining_capacity(const void *);
extern void cplus_alloc_fail_after(long);
extern void cplus_alloc_fail_reset(void);
extern void cplus_alloc_meta_register(void *, size_t, size_t); /* -1 on failure */
extern int cplus_transfer_ownership(void **, void **);
extern unsigned long cplus_alloc_double_free_count(void);
extern unsigned long cplus_alloc_invalid_free_count(void);
extern void *cplus_aligned_alloc(size_t, size_t);
extern char *cplus_strdup(const char *);
extern char *cplus_strndup(const char *, size_t);
#endif

/* ---------------------------------------------------------------- layout -- */
/* Fixed-width fields keep the layout independent of the host data model.
 * Static assertions in both producer and consumer TUs pin sizes/offsets. */
typedef struct pr31_record {
    uint32_t magic;
    uint16_t version;
    uint16_t flags;
    int32_t x;
    int32_t y;
    double weight;
    void *opaque;
    char tag[16];
} pr31_record;

typedef union pr31_union {
    uint64_t u64;
    double d;
    struct { uint32_t lo, hi; } halves;
    unsigned char bytes[8];
} pr31_union;

#define PR31_RECORD_MAGIC 0x50523331u
#define PR31_RECORD_VERSION 7u
#define PR31_RECORD_FLAGS 0x00A5u

/* ----------------------------------------------------------- opaque type -- */
typedef struct pr31_handle *pr31_handle; /* definition stays inside TU A */

/* ------------------------------------------------------- callbacks / varargs */
typedef int (*pr31_cb2_t)(int, int);
typedef int (*pr31_vsum_t)(int, ...);
typedef int (*pr31_vlist_t)(int, va_list);

/* ----------------------------------------------------------- producer API -- */
extern void *pr31_a_malloc(size_t);
extern void *pr31_a_calloc(size_t, size_t);
extern void *pr31_a_realloc(void *, size_t);
extern void *pr31_a_aligned_alloc(size_t, size_t);
extern char *pr31_a_strdup(const char *);
extern char *pr31_a_strndup(const char *, size_t);
extern int pr31_a_release_checked(void **);      /* cplus_alloc_checked_free */
extern int pr31_a_release_fc(void **);            /* cplus_free_checked */
extern void pr31_a_release(void *);               /* cplus_free */
extern int pr31_a_is_live(const void *);
extern int pr31_a_register(void *, size_t);
extern unsigned long pr31_a_invalid_frees(void);
extern unsigned long pr31_a_double_frees(void);
extern void pr31_a_fail_after(long);
extern void pr31_a_fail_reset(void);
extern int pr31_a_atomic_realloc(void **, size_t);
extern void pr31_a_set_transfer_src(void *);
extern int pr31_a_transfer_to(void **);
extern void pr31_a_fill_record(pr31_record *, const pr31_union *);
extern void pr31_a_fill_union(pr31_union *);
extern pr31_handle pr31_a_make_handle(int);
extern int pr31_a_check_handle(pr31_handle, int);
extern int pr31_a_call_cb(pr31_cb2_t, int, int);
extern int pr31_a_vsum(int, ...);
extern pr31_vsum_t pr31_a_vsum_ptr(void);
extern void pr31_a_set_vlist_cb(pr31_vlist_t);
extern int pr31_a_call_vlist_cb(int, ...);
extern int pr31_a_get_errno(void);

/* ----------------------------------------------------------- consumer API -- */
extern int pr31_b_add(int, int);
extern int pr31_b_vlist_sum(int, va_list);
extern int pr31_b_check_record(const pr31_record *, const pr31_union *);
extern void pr31_b_set_errno(int);
extern int pr31_b_use_handle(pr31_handle, int);
extern int pr31_b_release(void **);            /* direct cross-TU free */

#endif /* PR31_SHARED_H */
