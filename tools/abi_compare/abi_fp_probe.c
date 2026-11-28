/* abi_fp_probe.c -- ABI fingerprint probe for the C-compatible public surface.
 *
 * This is a pure C, compile-time + deterministic-print probe.  It never
 * allocates, never calls a library function from the project, and touches no
 * global state.  It prints one canonical record per line in the form
 *
 *     <SECTION><TAB><key><TAB><value>
 *
 * where SECTION is:
 *     V  tool/format metadata (emitted by run.sh, not here)
 *     L  layout facts: sizeof / _Alignof / offsetof / enum values
 *     C  compiler ABI facts (emitted by run.sh via clang -dM)
 *     S  exported symbols (emitted by run.sh via nm)
 *     F  function-signature probe results (below)
 *
 * The fixed set of public types below is the set declared in the installed
 * libc and stdlib/safe headers that is safe to consume from ordinary ISO C.
 * It mirrors the authoritative
 * layout table in tests/abi/abi_layouts.c; nothing here is used as a
 * correctness oracle at runtime -- the records are captured and later diffed
 * against a committed baseline by run.sh.
 *
 * Typical build (see run.sh for the exact command):
 *   /usr/bin/clang -O2 -std=c11 -Wall -Wextra -Werror <includes> \
 *       tools/abi_compare/abi_fp_probe.c -o abi_fp_probe
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* CPLUS_LIBC_SAFE_DISABLE keeps the plain-C safety header declaration-only so
 * it does not redefine printf/malloc/etc. macros used by this probe. */
#define CPLUS_LIBC_SAFE_DISABLE 1

/* ---- public headers under test ------------------------------------------ */
#include "cplus_libc.h"        /* cplus_alloc_meta, core allocation API */
#include "cplus_libc_safe.h"   /* plain-C hardened layer (declarations only) */
#include "cplus_threads.h"     /* cplus_thrd_t .. cplus_once_flag, anon enum */
#include "cplus_uchar.h"       /* char16_t / char32_t, mbstate_t adapter */
#include "safe_alloc.h"        /* cplus_safe_alloc_meta */
#include "safe_stats.h"        /* cplus_safe_stat */
#include "safe_platform.h"     /* cplus_safe_platform_info */
#include "safe_log.h"          /* cplus_safe_log_level */
#include "safe_error.h"        /* cplus_safe_constraint_mode */
#include "safe_runtime.h"      /* cplus_runtime_handler */
#include "safe_string.h"       /* errno_t, rsize_t */

/* Some installed headers may install a printf macro; this probe prints via
 * the real host printf so the record stream stays stable. */
#undef printf

/* C11 _Alignof is used deliberately (the probe is built with -std=c11). */
#define FP_ALIGNOF(t) _Alignof(t)

static void fp_rec_u(const char *sec, const char *key, unsigned long long v)
{
    printf("%s\t%s\t%llu\n", sec, key, v);
}

static void fp_rec_s(const char *sec, const char *key, const char *v)
{
    printf("%s\t%s\t%s\n", sec, key, v);
}

/* ---- printing helpers for layout facts ---------------------------------- */
#define L_SIZE(T)   fp_rec_u("L", "size " #T, (unsigned long long)sizeof(T))
#define L_ALIGN(T)  fp_rec_u("L", "align " #T, (unsigned long long)FP_ALIGNOF(T))
#define L_OFF(T, F) fp_rec_u("L", "offset " #T "." #F, (unsigned long long)offsetof(T, F))
#define L_VAL(E)    fp_rec_u("L", "value " #E, (unsigned long long)(E))

/* ---- compile-time function-signature probe ------------------------------
 * For each public function we assert that the type declared by the header is
 * compatible with the expected ABI signature.  A header signature change
 * turns into a compile error (the tool then fails loudly rather than silently
 * recording a stale fingerprint).  We additionally emit a human-readable
 * signature string into the fingerprint so additions/removals of probed
 * functions are visible as diffs.
 */
typedef void   *fp_sig_malloc(size_t);
typedef void    fp_sig_free(void *);
typedef size_t  fp_sig_alloc_size(const void *);
typedef int     fp_sig_alloc_get_meta(const void *, cplus_alloc_meta *);
typedef size_t  fp_sig_alloc_rem_cap(const void *);
typedef errno_t fp_sig_strcpy_s(char *, rsize_t, const char *);
typedef rsize_t fp_sig_strnlen_s(const char *, rsize_t);
typedef void    fp_sig_set_handler(cplus_runtime_handler);
typedef int     fp_sig_thrd_create(cplus_thrd_t *, cplus_thrd_start_t, void *);
typedef int     fp_sig_thrd_join(cplus_thrd_t, int *);
typedef void    fp_sig_platform_probe(cplus_safe_platform_info *);
typedef int     fp_sig_stats_get(const char *, cplus_safe_stat *);

#define SIG_CHECK(fn, sig, text)                                              \
    _Static_assert(__builtin_types_compatible_p(__typeof__(fn), sig),        \
                   "ABI signature changed: " #fn);                           \
    fp_rec_s("F", "signature " #fn, (text))

int main(void)
{
    /* ---- data model / pointer width (detect LP64 -> ILP32 etc.) ---------- */
    L_SIZE(void *);        L_ALIGN(void *);
    L_SIZE(char *);        L_ALIGN(char *);
    L_SIZE(int *);
    L_SIZE(int (*)(int));  L_ALIGN(int (*)(int));
    L_SIZE(size_t);        L_ALIGN(size_t);
    L_SIZE(uintptr_t);
    L_SIZE(int);
    L_SIZE(long);          L_ALIGN(long);
    L_SIZE(unsigned long);
    L_SIZE(long long);
    L_SIZE(double);
    L_SIZE(long double);
    L_SIZE(max_align_t);   L_ALIGN(max_align_t);

    /* ---- libc/install-include/cplus_libc.h : metadata side table -------- */
    L_SIZE(cplus_alloc_meta);
    L_ALIGN(cplus_alloc_meta);
    L_OFF(cplus_alloc_meta, byte_size);
    L_OFF(cplus_alloc_meta, elem_size);
    L_OFF(cplus_alloc_meta, elem_count);
    L_OFF(cplus_alloc_meta, generation);
    L_OFF(cplus_alloc_meta, is_live);

    /* ---- libc/install-include/cplus_threads.h --------------------------- */
    L_SIZE(cplus_thrd_t);        L_ALIGN(cplus_thrd_t);
    L_SIZE(cplus_mtx_t);         L_ALIGN(cplus_mtx_t);
    L_SIZE(cplus_cnd_t);         L_ALIGN(cplus_cnd_t);
    L_SIZE(cplus_once_flag);     L_ALIGN(cplus_once_flag);
    L_SIZE(cplus_tss_t);         L_ALIGN(cplus_tss_t);
    L_SIZE(cplus_thrd_start_t);  L_ALIGN(cplus_thrd_start_t);
    L_VAL(CPLUS_THRD_SUCCESS);
    L_VAL(CPLUS_THRD_BUSY);
    L_VAL(CPLUS_THRD_NOMEM);
    L_VAL(CPLUS_THRD_TIMEDOUT);
    L_VAL(CPLUS_THRD_ERROR);

    /* ---- libc/install-include/cplus_uchar.h ----------------------------- */
    L_SIZE(char16_t);  L_ALIGN(char16_t);
    L_SIZE(char32_t);  L_ALIGN(char32_t);
    L_SIZE(mbstate_t); L_ALIGN(mbstate_t);

    /* ---- stdlib/safe/safe_alloc.h --------------------------------------- */
    L_SIZE(cplus_safe_alloc_meta);
    L_ALIGN(cplus_safe_alloc_meta);
    L_OFF(cplus_safe_alloc_meta, byte_size);
    L_OFF(cplus_safe_alloc_meta, elem_size);
    L_OFF(cplus_safe_alloc_meta, elem_count);
    L_OFF(cplus_safe_alloc_meta, generation);
    L_OFF(cplus_safe_alloc_meta, is_live);

    /* ---- stdlib/safe/safe_stats.h --------------------------------------- */
    L_SIZE(cplus_safe_stat);  L_ALIGN(cplus_safe_stat);
    L_OFF(cplus_safe_stat, api);
    L_OFF(cplus_safe_stat, calls);
    L_OFF(cplus_safe_stat, ok);
    L_OFF(cplus_safe_stat, fail);

    /* ---- stdlib/safe/safe_platform.h ------------------------------------ */
    L_SIZE(cplus_safe_platform_info);  L_ALIGN(cplus_safe_platform_info);
    L_OFF(cplus_safe_platform_info, has_fortify);
    L_OFF(cplus_safe_platform_info, has_object_size);
    L_OFF(cplus_safe_platform_info, has_annex_k);
    L_OFF(cplus_safe_platform_info, has_apple_libc);
    L_OFF(cplus_safe_platform_info, has_ucrt);
    L_OFF(cplus_safe_platform_info, has_glibc);
    L_OFF(cplus_safe_platform_info, has_musl);

    /* ---- stdlib/safe/safe_log.h ----------------------------------------- */
    L_SIZE(cplus_safe_log_level);  L_ALIGN(cplus_safe_log_level);
    L_VAL(CPLUS_SAFE_LOG_OFF);
    L_VAL(CPLUS_SAFE_LOG_ERROR);
    L_VAL(CPLUS_SAFE_LOG_WARN);
    L_VAL(CPLUS_SAFE_LOG_INFO);
    L_VAL(CPLUS_SAFE_LOG_DEBUG);
    L_VAL(CPLUS_SAFE_LOG_TRACE);

    /* ---- stdlib/safe/safe_error.h --------------------------------------- */
    L_SIZE(cplus_safe_constraint_mode);  L_ALIGN(cplus_safe_constraint_mode);
    L_VAL(CPLUS_SAFE_CONSTRAINT_IGNORE);
    L_VAL(CPLUS_SAFE_CONSTRAINT_ABORT);

    /* ---- stdlib/safe/safe_runtime.h / safe_string.h --------------------- */
    L_SIZE(cplus_runtime_handler);  L_ALIGN(cplus_runtime_handler);
    L_SIZE(errno_t);  L_ALIGN(errno_t);
    L_SIZE(rsize_t);  L_ALIGN(rsize_t);

    /* ---- function-signature probe (compile-time asserted above) --------- */
    SIG_CHECK(cplus_malloc,                 fp_sig_malloc,
              "void *(size_t)");
    SIG_CHECK(cplus_free,                   fp_sig_free,
              "void(void *)");
    SIG_CHECK(cplus_alloc_size,             fp_sig_alloc_size,
              "size_t(const void *)");
    SIG_CHECK(cplus_alloc_get_meta,         fp_sig_alloc_get_meta,
              "int(const void *, cplus_alloc_meta *)");
    SIG_CHECK(cplus_alloc_remaining_capacity, fp_sig_alloc_rem_cap,
              "size_t(const void *)");
    SIG_CHECK(cplus_strcpy_s,               fp_sig_strcpy_s,
              "errno_t(char *, rsize_t, const char *)");
    SIG_CHECK(cplus_strnlen_s,              fp_sig_strnlen_s,
              "rsize_t(const char *, rsize_t)");
    SIG_CHECK(cplus_runtime_set_handler,    fp_sig_set_handler,
              "void(cplus_runtime_handler)");
    SIG_CHECK(cplus_thrd_create,            fp_sig_thrd_create,
              "int(cplus_thrd_t *, cplus_thrd_start_t, void *)");
    SIG_CHECK(cplus_thrd_join,              fp_sig_thrd_join,
              "int(cplus_thrd_t, int *)");
    SIG_CHECK(cplus_safe_platform_probe,    fp_sig_platform_probe,
              "void(cplus_safe_platform_info *)");
    SIG_CHECK(cplus_safe_stats_get,         fp_sig_stats_get,
              "int(const char *, cplus_safe_stat *)");

    return 0;
}
