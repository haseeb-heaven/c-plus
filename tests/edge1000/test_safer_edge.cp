/* Safer-lib edge sweep (Issue #24, added 2026-11-24).
 *
 * Deterministic edge cases over the safer libs: stdlib/safe/safe_alloc.h
 * (malloc_s/calloc_s/realloc_s/free_s + live/size queries),
 * stdlib/safe/safe_string.h (cplus_strcpy_s/strnlen_s),
 * stdlib/safe/safe_diag.h (getters), stdlib/safe/safe_error.h (constraint
 * mode get/set), stdlib/safe/safe_platform.h (probe),
 * stdlib/safe/safe_stats.h (reset/get).  N=1200 loop iterations.
 *
 * Prints a summary `EDGE1000_SAFER <cases> <pass>`; exit 0 == all pass.
 */
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/safe/safe_alloc.h"
#include "stdlib/safe/safe_string.h"
#include "stdlib/safe/safe_diag.h"
#include "stdlib/safe/safe_error.h"
#include "stdlib/safe/safe_platform.h"
#include "stdlib/safe/safe_stats.h"

static int g_cases = 0;
static int g_pass = 0;

static void edge(int cond) {
    g_cases++;
    if (cond) {
        g_pass++;
    } else {
        printf("EDGE1000_FAIL safer case %d\n", g_cases);
    }
}

int main(void) {
    const int N = 1200;
    int i;
    for (i = 0; i < N; i++) {
        void *p;
        void *q;
        errno_t rc;
        rsize_t sz;
        char dst[32];

        p = malloc_s((size_t)(i % 4));
        edge(p != (void *)0);
        q = calloc_s((size_t)(i % 3 + 1), (size_t)(i + 1));
        edge(q != (void *)0);
        free_s(p);
        edge(cplus_safe_ptr_was_freed(p));
        p = realloc_s(q, (size_t)(i % 64));
        edge(p != (void *)0);
        free_s(p);

        /* Oversized allocations are refused, not fatal; freed pointers are
         * reported as not-live after free. */
        if (i % 8 == 0)
            edge(malloc_s((size_t)(RSIZE_MAX_MEM + 1)) == (void *)0);
        p = malloc_s(16);
        edge(p != (void *)0);
        edge(cplus_safe_ptr_size(p) >= 16);
        free_s(p);
        free_s((void *)0);           /* NULL free: documented no-op */
        free_s(p);                   /* double free: refused, no crash */
        edge(cplus_safe_ptr_was_freed(p));

        /* String edges: oversized source refused, exact fit ok, nlen ok. */
        memset(dst, 'x', sizeof(dst));
        rc = cplus_strcpy_s(dst, (rsize_t)3, "abcd");
        edge(rc != 0);
        rc = cplus_strcpy_s(dst, (rsize_t)4, "abc");
        edge(rc == 0);
        sz = cplus_strnlen_s("abc", (rsize_t)10);
        edge(sz == 3);

        /* Diagnostics, error-mode and platform-probe edges. */
        cplus_diag_get_mode();
        cplus_diag_get_color();
        cplus_diag_color_enabled((FILE *)0);
        cplus_safe_get_constraint_mode();
        cplus_safe_platform_probe((cplus_safe_platform_info *)0);
        cplus_safe_stats_reset();
        edge(cplus_safe_stats_dump((cplus_safe_stat *)0, (size_t)0) == 0);
        cplus_safe_stat rec;
        edge(cplus_safe_stats_get((const char *)"malloc_s", &rec) <= 1);
    }
    printf("EDGE1000_SAFER %d %d\n", g_cases, g_pass);
    return (g_cases == g_pass) ? 0 : 1;
}
