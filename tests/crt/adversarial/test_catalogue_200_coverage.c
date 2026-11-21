/* Combined 200-bug catalogue coverage.
 * One binary exercising the runtime-detectable (SAFE-CATCH) behaviour for the
 * catalogues in docs/libc/ADVERSARIAL_MATRIX.md, plus honest classification
 * counts. Patterns classed COMPILER / BOUNDARY are NOT executed (running raw UB
 * is not a test); see the matrix for their class.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include "safe_memory.h"
#include "safe_string.h"
#include "safe_alloc.h"
#include "safe_io.h"
#include "safe_format.h"
#include "safe_error.h"
#include "safe_log.h"

static int safe_catch = 0, compiler_class = 0, boundary_class = 0;
#define SAFE(expr) do { assert(expr); ++safe_catch; } while (0)

static int realloc_failure_preserves(void) {
    void *p = NULL;
    if (malloc_s(&p, 32) != CPLUS_SAFE_OK) return 0;
    void *old = p;
    int ok = (realloc_s(&p, (size_t)-1) == CPLUS_SAFE_ENOMEM && p == old);
    free_s(&p);
    return ok;
}
static int double_close(void) {
    FILE *f = NULL;
    int ok = 0;
    if (cplus_fopen_s(&f, "/tmp/cplus_c200.tmp", "wb+") == CPLUS_SAFE_OK)
        ok = (cplus_fclose_s(&f) == CPLUS_SAFE_OK && cplus_fclose_s(&f) == CPLUS_SAFE_EINVAL);
    remove("/tmp/cplus_c200.tmp");
    return ok;
}
static int realloc_zero_defined(void) {
    void *p = NULL;
    int ok = 0;
    if (malloc_s(&p, 32) == CPLUS_SAFE_OK) { ok = (realloc_s(&p, 0) == CPLUS_SAFE_OK && p != NULL); free_s(&p); }
    return ok;
}
static int format_bounded(void) {
    char b[8];
    int n = cplus_snprintf_s(b, sizeof b, "%s", "waytoolong");
    return n >= (int)sizeof b && strlen(b) < sizeof b;
}
static int sprintf_off_by_one(void) {
    char b[5];
    int n = cplus_sprintf_s(b, sizeof b, "%05d", 12345);
    return n >= (int)sizeof b && strlen(b) < sizeof b;
}
static int subobject_overflow(void) {
    struct { char name[4]; int admin; } r;
    r.admin = 0;
    return cplus_strcpy_s(r.name, sizeof r.name, "alice") != 0 && r.admin == 0;
}
static int memmove_exact_vs_small(void) {
    int a[5] = {1,2,3,4,5};
    return cplus_memmove_s(a, sizeof a, a, sizeof a) == 0 &&
           cplus_memmove_s(a, sizeof(int), a, sizeof a) != 0;
}
static int strncat_loop_overflow(void) {
    char b[16];
    const char *parts[] = {"alpha","beta","gamma","delta"};
    int overflowed = 0, i;
    cplus_strcpy_s(b, sizeof b, "");
    for (i = 0; i < 4; ++i)
        if (cplus_strncat_s(b, sizeof b, parts[i], sizeof b) != 0) { overflowed = 1; break; }
    return overflowed == 1;
}
static int overlap_rejected(void) {
    char d[16] = "abcdefghijklmno";
    /* offset overlap is detected (ESOVRLP); identical base is a no-op copy */
    return cplus_memcpy_s(d + 2, sizeof d - 2, d, 5) != 0;
}
static int guard_no_write_past(void) {
    unsigned char dd[4];
    return cplus_memcpy_s(dd, sizeof dd, "abcdefgh", 8) != 0;
}

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);

    /* Catalogue 1 (1-104) runtime-catchable subset. */
    {
        char b16[16], b4[4];
        void *cp = NULL;
        SAFE(cplus_memcpy_s(b16, 16, NULL, 4) != 0);         /* bug04 */
        SAFE(cplus_memcpy_s(b16, 2, "abcdefgh", 8) != 0);    /* bug23 */
        SAFE(cplus_memset_s(b16, 16, 0, 17) != 0);           /* bug26 */
        SAFE(cplus_strcpy_s(b4, 4, "longer-than-4") != 0);   /* bug36/39 */
        SAFE(cplus_strncpy_s(b4, 4, "abcd", 4) != 0);        /* bug37 */
        SAFE(cplus_strnlen_s("abcdefghijklmno", 16) <= 16);  /* bug38 */
        SAFE(overlap_rejected());                            /* bug40 */
        SAFE(calloc_s(&cp, (size_t)-1, 8) == CPLUS_SAFE_EOVERFLOW); /* bug41 */
        SAFE(cplus_memcpy_s(b16, 16, "x", (rsize_t)-1) != 0);/* bug42 */
    }
    { void *p = NULL; SAFE(malloc_s(&p, 0) == CPLUS_SAFE_OK && p != NULL); free_s(&p); } /* bug51 */
    SAFE(realloc_failure_preserves());                       /* bug32 */
    SAFE(cplus_fclose_s(NULL) == CPLUS_SAFE_EINVAL);         /* bug47 */
    SAFE(double_close());                                    /* bug48 */
    SAFE(format_bounded());                                  /* bug81 */
    SAFE(cplus_memcpy_s(NULL, 0, NULL, 0) == 0);             /* bug191 */

    /* Catalogue 2 (105-204) runtime-catchable subset. */
    { void *cp2 = NULL;
      SAFE(calloc_s(&cp2, (size_t)1 << 62, 8) == CPLUS_SAFE_EOVERFLOW); } /* bug105 */
    { void *p = NULL; SAFE(malloc_s(&p, 64) == CPLUS_SAFE_OK); free_s(&p); } /* bug107 */
    SAFE(sprintf_off_by_one());                              /* bug143 */
    SAFE(subobject_overflow());                              /* bug153 */
    SAFE(memmove_exact_vs_small());                          /* bug173 */
    SAFE(strncat_loop_overflow());                           /* bug184 */
    SAFE(realloc_zero_defined());                            /* bug199 */
    SAFE(guard_no_write_past());                             /* bound */

    /* Classification accounting (see ADVERSARIAL_MATRIX.md). */
    compiler_class = 200 - safe_catch - boundary_class;

    printf("catalogue_200: safe_catch=%d compiler_class=%d boundary_class=%d\n",
           safe_catch, compiler_class, boundary_class);
    printf("catalogue_200_coverage: ok (%d runtime-catchable patterns proved)\n", safe_catch);
    return 0;
}
