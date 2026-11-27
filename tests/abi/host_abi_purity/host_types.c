/* Host-C / C+ public-struct layout parity probe (issues #186, #187).
 *
 * Compiled twice with the same host compiler:
 *   baseline:  plain ISO C, no C+ or hardened headers
 *   with:      -DCPLUS_ABI_PURITY_WITH_HEADERS plus the public include dirs
 *
 * Both runs print canonical `key<TAB>value` records.  The two outputs must be
 * byte-identical.  In the "with" build, compile-time assertions additionally
 * require each real public struct to match an independently written plain-C
 * mirror, so a new hidden field or a reordered field fails the build.
 *
 * Macro rewriting is explicitly OFF in the "with" build
 * (CPLUS_SAFE_C_DISABLE / CPLUS_LIBC_SAFE_DISABLE): this test measures layout
 * and declaration-only header inclusion, not the opt-in safe_c.h macros.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <pthread.h>
#include <time.h>
#include <setjmp.h>
#include <stdatomic.h>

/* ---- Independent plain-C mirrors of the public ABI structs -------------- */
typedef struct mirror_alloc_meta {
    size_t byte_size;
    size_t elem_size;
    size_t elem_count;
    unsigned long long generation;
    int is_live;
} mirror_alloc_meta;

typedef struct mirror_safe_stat {
    char api[24];                 /* CPLUS_SAFE_STATS_API_MAX */
    unsigned long calls;
    unsigned long ok;
    unsigned long fail;
} mirror_safe_stat;

typedef struct mirror_platform_info {
    int has_fortify, has_object_size, has_annex_k, has_apple_libc;
    int has_ucrt, has_glibc, has_musl;
} mirror_platform_info;

typedef enum mirror_cleanup_kind {
    MIRROR_CLEANUP_MANAGED = 0,
    MIRROR_CLEANUP_RESOURCE = 1
} mirror_cleanup_kind;

typedef struct mirror_cleanup {
    void *ptr;
    void (*drop)(void *self);
    mirror_cleanup_kind kind;
    struct mirror_cleanup *next;
} mirror_cleanup;

typedef struct mirror_scope { mirror_cleanup *head; } mirror_scope;

typedef struct mirror_exception {
    int code;
    const char *message;
    const char *file;
    int line;
} mirror_exception;

typedef struct mirror_frame {
    jmp_buf env;
    struct mirror_frame *prev;
    mirror_exception exc;
    uint64_t generation;
} mirror_frame;

#ifdef CPLUS_ABI_PURITY_WITH_HEADERS
#define CPLUS_SAFE_C_DISABLE 1
#define CPLUS_LIBC_SAFE_DISABLE 1
#include "cplus_libc.h"
#include "cplus_libc_safe.h"
#include "cplus_threads.h"
#include "cplus_uchar.h"
#include "cplus_stdatomic.h"
#include "safe_alloc.h"
#include "safe_stats.h"
#include "safe_platform.h"
#include "safe_string.h"
#include "cplus/cplus.h"

#define T_alloc_meta cplus_alloc_meta
#define T_safe_meta cplus_safe_alloc_meta
#define T_stat cplus_safe_stat
#define T_platform cplus_safe_platform_info
#define T_cleanup cplus_cleanup
#define T_scope cplus_scope
#define T_frame CPlusExceptionFrame

_Static_assert(sizeof(cplus_alloc_meta) == sizeof(mirror_alloc_meta),
               "cplus_alloc_meta layout changed");
_Static_assert(_Alignof(cplus_alloc_meta) == _Alignof(mirror_alloc_meta),
               "cplus_alloc_meta alignment changed");
_Static_assert(offsetof(cplus_alloc_meta, is_live) ==
               offsetof(mirror_alloc_meta, is_live),
               "cplus_alloc_meta field order changed");
_Static_assert(sizeof(cplus_safe_alloc_meta) == sizeof(mirror_alloc_meta),
               "cplus_safe_alloc_meta layout changed");
_Static_assert(sizeof(cplus_safe_stat) == sizeof(mirror_safe_stat),
               "cplus_safe_stat layout changed");
_Static_assert(offsetof(cplus_safe_stat, fail) ==
               offsetof(mirror_safe_stat, fail),
               "cplus_safe_stat field order changed");
_Static_assert(sizeof(cplus_safe_platform_info) == sizeof(mirror_platform_info),
               "cplus_safe_platform_info layout changed");
_Static_assert(sizeof(cplus_cleanup) == sizeof(mirror_cleanup),
               "cplus_cleanup layout changed");
_Static_assert(offsetof(cplus_cleanup, next) == offsetof(mirror_cleanup, next),
               "cplus_cleanup field order changed");
_Static_assert(sizeof(cplus_scope) == sizeof(mirror_scope),
               "cplus_scope layout changed");
_Static_assert(sizeof(CPlusExceptionFrame) == sizeof(mirror_frame),
               "CPlusExceptionFrame layout changed");
_Static_assert(offsetof(CPlusExceptionFrame, generation) ==
               offsetof(mirror_frame, generation),
               "CPlusExceptionFrame field order changed");
_Static_assert(sizeof(cplus_atomic_flag) == 1,
               "cplus_atomic_flag must stay one byte");
#else
#define T_alloc_meta mirror_alloc_meta
#define T_safe_meta mirror_alloc_meta
#define T_stat mirror_safe_stat
#define T_platform mirror_platform_info
#define T_cleanup mirror_cleanup
#define T_scope mirror_scope
#define T_frame mirror_frame
#endif

/* ---- canonical record helpers ------------------------------------------- */
static void rec_u(const char *kind, const char *name, unsigned long long v)
{
    printf("%s %s\t%llu\n", kind, name, v);
}

#define SIZE(kind, name, T)  rec_u("size", name, (unsigned long long)sizeof(T))
#define ALIGN(kind, name, T) rec_u("align", name, (unsigned long long)_Alignof(T))
#define OFF(kind, name, T, F) \
    rec_u("offset", name, (unsigned long long)offsetof(T, F))

int main(void)
{
    /* Pointer-width and data-model invariants on both sides of the boundary. */
    _Static_assert(sizeof(void *) == sizeof(uintptr_t), "pointer/uintptr mismatch");
    _Static_assert(sizeof(size_t) == sizeof(uintptr_t), "size_t/uintptr mismatch");

    /* Standard C host types: identical records with and without C+ headers. */
    SIZE("host", "FILE", FILE);
    ALIGN("host", "FILE", FILE);
    SIZE("host", "mbstate_t", mbstate_t);
    ALIGN("host", "mbstate_t", mbstate_t);
    SIZE("host", "pthread_t", pthread_t);
    ALIGN("host", "pthread_t", pthread_t);
    SIZE("host", "pthread_mutex_t", pthread_mutex_t);
    SIZE("host", "atomic_int", _Atomic int);
    SIZE("host", "div_t", div_t);
    OFF("host", "div_t.quot", div_t, quot);
    SIZE("host", "ldiv_t", ldiv_t);
    SIZE("host", "timespec", struct timespec);
    OFF("host", "timespec.tv_sec", struct timespec, tv_sec);
    SIZE("host", "tm", struct tm);
    ALIGN("host", "tm", struct tm);
    OFF("host", "tm.tm_year", struct tm, tm_year);
    SIZE("host", "fpos_t", fpos_t);

    /* Public C+ structs: real types in the "with" build, mirrors otherwise. */
    SIZE("api", "cplus_alloc_meta", T_alloc_meta);
    ALIGN("api", "cplus_alloc_meta", T_alloc_meta);
    OFF("api", "cplus_alloc_meta.is_live", T_alloc_meta, is_live);
    SIZE("api", "cplus_safe_alloc_meta", T_safe_meta);
    OFF("api", "cplus_safe_alloc_meta.generation", T_safe_meta, generation);
    SIZE("api", "cplus_safe_stat", T_stat);
    OFF("api", "cplus_safe_stat.fail", T_stat, fail);
    SIZE("api", "cplus_safe_platform_info", T_platform);
    SIZE("api", "cplus_cleanup", T_cleanup);
    OFF("api", "cplus_cleanup.next", T_cleanup, next);
    SIZE("api", "cplus_scope", T_scope);
    SIZE("api", "CPlusExceptionFrame", T_frame);
    OFF("api", "CPlusExceptionFrame.generation", T_frame, generation);
    return 0;
}
