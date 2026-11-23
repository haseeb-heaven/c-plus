/* Test 2: C-compatible public layouts checked against captured constants.
 *
 * This is a NEW probe (tests/abi/abi_layouts.c is deliberately untouched).
 * The expected values below are the ones currently produced by the checked-in
 * public headers when compiled by the real host C compiler on
 * arm64-apple-darwin / Apple clang 16; they are transcribed verbatim from the
 * golden table in tests/abi/abi_layouts.c and re-verified here.  Every expected
 * value is also documented in ABI_COMPAT_NOTES.md.
 *
 *   /usr/bin/clang -O1 -std=c99 -Wall -Wextra -pedantic \
 *     -Ilibc -Ilibc/install-include -Istdlib/safe \
 *     -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include \
 *     tests/abi/abi_compat_layouts.c \
 *     build/libc/libcplus_hardened.a build/safe/libcplus_safecrt.a \
 *     -lm -pthread -o build/abi/compat/layouts
 */
#include <stddef.h>
#include <stdio.h>

/* Public headers under test. */
#include "cplus_libc.h"      /* cplus_alloc_meta + metadata side-table API */
#include "cplus_libc_safe.h" /* ordinary-C macro layer (types come from libc) */
#include "cplus_threads.h"   /* cplus_thrd_t .. cplus_once_flag, anon enum */
#include "cplus_uchar.h"     /* char16_t / char32_t, mbstate_t adapter */
#include "safe_alloc.h"      /* cplus_safe_alloc_meta */
#include "safe_stats.h"      /* cplus_safe_stat */
#include "safe_platform.h"   /* cplus_safe_platform_info */
#include "safe_log.h"        /* cplus_safe_log_level */
#include "safe_error.h"      /* cplus_safe_constraint_mode */
#include "safe_runtime.h"    /* cplus_runtime_handler */
#include "safe_string.h"     /* errno_t, rsize_t */
#include "abi_compat_shared.h"

/* Avoid the safe-layer printf macro in this particular probe: we want the
 * native host formatting and an exact, stable line format for the log. */
#undef printf

#define ABI_ALIGNOF(t) __alignof__(t)

static int g_checks;
static int g_failed;

static void abi_check(const char *kind, const char *name,
                      size_t actual, size_t expected)
{
    ++g_checks;
    if (actual != expected)
        ++g_failed;
    printf("%-7s %-52s actual=%-5llu expected=%-5llu %s\n",
           kind, name,
           (unsigned long long)actual, (unsigned long long)expected,
           actual == expected ? "OK" : "MISMATCH");
}

#define CHK_SIZE(T, E)  abi_check("size",   #T,        sizeof(T),       (size_t)(E))
#define CHK_ALIGN(T, E) abi_check("align",  #T,        ABI_ALIGNOF(T),  (size_t)(E))
#define CHK_OFF(T, F, E) abi_check("offset", #T "." #F, offsetof(T, F), (size_t)(E))
#define CHK_VALUE(E, V) abi_check("value",  #E,        (size_t)(E),     (size_t)(V))

int main(void)
{
    printf("abi_compat_layouts: baseline sizeof(void*)=%llu "
           "sizeof(size_t)=%llu sizeof(unsigned long)=%llu\n",
           (unsigned long long)sizeof(void *),
           (unsigned long long)sizeof(size_t),
           (unsigned long long)sizeof(unsigned long));

    /* ---- libc/install-include/cplus_libc.h : metadata side table -------- */
    CHK_SIZE(cplus_alloc_meta, 40);
    CHK_ALIGN(cplus_alloc_meta, 8);
    CHK_OFF(cplus_alloc_meta, byte_size, 0);
    CHK_OFF(cplus_alloc_meta, elem_size, 8);
    CHK_OFF(cplus_alloc_meta, elem_count, 16);
    CHK_OFF(cplus_alloc_meta, generation, 24);
    CHK_OFF(cplus_alloc_meta, is_live, 32);

    /* ---- libc/install-include/cplus_threads.h --------------------------- */
    CHK_SIZE(cplus_thrd_t, 32);
    CHK_ALIGN(cplus_thrd_t, 8);
    CHK_SIZE(cplus_mtx_t, 128);
    CHK_ALIGN(cplus_mtx_t, 8);
    CHK_SIZE(cplus_cnd_t, 128);
    CHK_ALIGN(cplus_cnd_t, 8);
    CHK_SIZE(cplus_once_flag, 64);
    CHK_ALIGN(cplus_once_flag, 8);
    CHK_SIZE(cplus_tss_t, 8);
    CHK_ALIGN(cplus_tss_t, 8);
    CHK_SIZE(cplus_thrd_start_t, 8);
    CHK_ALIGN(cplus_thrd_start_t, 8);
    CHK_VALUE(CPLUS_THRD_SUCCESS, 0);
    CHK_VALUE(CPLUS_THRD_BUSY, 1);
    CHK_VALUE(CPLUS_THRD_NOMEM, 2);
    CHK_VALUE(CPLUS_THRD_TIMEDOUT, 3);
    CHK_VALUE(CPLUS_THRD_ERROR, 4);

    /* ---- libc/install-include/cplus_uchar.h ----------------------------- */
    CHK_SIZE(char16_t, 2);
    CHK_ALIGN(char16_t, 2);
    CHK_SIZE(char32_t, 4);
    CHK_ALIGN(char32_t, 4);
    CHK_SIZE(mbstate_t, 128);
    CHK_ALIGN(mbstate_t, 8);

    /* ---- stdlib/safe/safe_alloc.h : mirror of the metadata struct ------- */
    CHK_SIZE(cplus_safe_alloc_meta, 40);
    CHK_ALIGN(cplus_safe_alloc_meta, 8);
    CHK_OFF(cplus_safe_alloc_meta, byte_size, 0);
    CHK_OFF(cplus_safe_alloc_meta, elem_size, 8);
    CHK_OFF(cplus_safe_alloc_meta, elem_count, 16);
    CHK_OFF(cplus_safe_alloc_meta, generation, 24);
    CHK_OFF(cplus_safe_alloc_meta, is_live, 32);

    /* ---- stdlib/safe/safe_stats.h --------------------------------------- */
    CHK_SIZE(cplus_safe_stat, 48);
    CHK_ALIGN(cplus_safe_stat, 8);
    CHK_OFF(cplus_safe_stat, api, 0);
    CHK_OFF(cplus_safe_stat, calls, 24);
    CHK_OFF(cplus_safe_stat, ok, 32);
    CHK_OFF(cplus_safe_stat, fail, 40);

    /* ---- stdlib/safe/safe_platform.h ------------------------------------ */
    CHK_SIZE(cplus_safe_platform_info, 28);
    CHK_ALIGN(cplus_safe_platform_info, 4);
    CHK_OFF(cplus_safe_platform_info, has_fortify, 0);
    CHK_OFF(cplus_safe_platform_info, has_object_size, 4);
    CHK_OFF(cplus_safe_platform_info, has_annex_k, 8);
    CHK_OFF(cplus_safe_platform_info, has_apple_libc, 12);
    CHK_OFF(cplus_safe_platform_info, has_ucrt, 16);
    CHK_OFF(cplus_safe_platform_info, has_glibc, 20);
    CHK_OFF(cplus_safe_platform_info, has_musl, 24);

    /* ---- stdlib/safe/safe_log.h / safe_error.h / safe_runtime.h --------- */
    CHK_SIZE(cplus_safe_log_level, 4);
    CHK_ALIGN(cplus_safe_log_level, 4);
    CHK_VALUE(CPLUS_SAFE_LOG_OFF, 0);
    CHK_VALUE(CPLUS_SAFE_LOG_ERROR, 1);
    CHK_VALUE(CPLUS_SAFE_LOG_WARN, 2);
    CHK_VALUE(CPLUS_SAFE_LOG_INFO, 3);
    CHK_VALUE(CPLUS_SAFE_LOG_DEBUG, 4);
    CHK_VALUE(CPLUS_SAFE_LOG_TRACE, 5);
    CHK_SIZE(cplus_safe_constraint_mode, 4);
    CHK_ALIGN(cplus_safe_constraint_mode, 4);
    CHK_VALUE(CPLUS_SAFE_CONSTRAINT_IGNORE, 0);
    CHK_VALUE(CPLUS_SAFE_CONSTRAINT_ABORT, 1);
    CHK_SIZE(cplus_runtime_handler, 8);
    CHK_ALIGN(cplus_runtime_handler, 8);
    CHK_SIZE(errno_t, 4);
    CHK_ALIGN(errno_t, 4);
    CHK_SIZE(rsize_t, 8);
    CHK_ALIGN(rsize_t, 8);

    /* ---- struct used for by-value / by-pointer boundary tests ----------- */
    CHK_SIZE(abi_compat_record, 32);
    CHK_ALIGN(abi_compat_record, 8);
    CHK_OFF(abi_compat_record, i, 0);
    CHK_OFF(abi_compat_record, u, 4);
    CHK_OFF(abi_compat_record, d, 8);
    CHK_OFF(abi_compat_record, p, 16);
    CHK_OFF(abi_compat_record, n, 24);

    /* ---- metadata side-table structural consistency --------------------- */
    abi_check("size", "libc vs SafeCRT meta struct (equal)",
              sizeof(cplus_alloc_meta), sizeof(cplus_safe_alloc_meta));
    abi_check("offset", "libc vs SafeCRT meta byte_size",
              offsetof(cplus_alloc_meta, byte_size),
              offsetof(cplus_safe_alloc_meta, byte_size));
    abi_check("offset", "libc vs SafeCRT meta is_live",
              offsetof(cplus_alloc_meta, is_live),
              offsetof(cplus_safe_alloc_meta, is_live));

    printf("abi_compat_layouts: %d/%d layouts match\n",
           g_checks - g_failed, g_checks);
    return g_failed == 0 ? 0 : 1;
}
