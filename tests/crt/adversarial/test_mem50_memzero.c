/* Adversarial checked-memory scenarios (M-A, file 6/10).
 *
 * Topic: cplus_memzero_s: zeroing of a bounded length, ESZEROL for a zero
 * length, ESNULLP for NULL, ESLEMAX for len > RSIZE_MAX_MEM, guard bytes past
 * the zeroed length, idempotence, and a looped stress run.
 *
 * Buffer is TOTAL bytes; LEN is a truthful sub-bound. Report-only.
 */
#include <stdio.h>
#include <string.h>
#include "safe_memory.h"
#include "safe_log.h"
#include "safe_error.h"
#include "safe_lib_errno.h"

#define TOTAL 16
#define LEN   8

static int failures = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            failures++;                                                        \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__,   \
                    #cond);                                                    \
        }                                                                      \
    } while (0)

static int all_eq(const unsigned char *p, size_t from, size_t to,
                  unsigned char v) {
    size_t k;
    for (k = from; k < to; k++)
        if (p[k] != v) return 0;
    return 1;
}

int main(void) {
    unsigned char buf[TOTAL];
    size_t i;

    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);

    /* (1) zero exactly LEN bytes; bytes past LEN stay intact */
    memset(buf, 0x5A, sizeof buf);
    CHECK(cplus_memzero_s(buf, LEN) == 0);
    CHECK(all_eq(buf, 0, LEN, 0x00));
    CHECK(all_eq(buf, LEN, TOTAL, 0x5A));

    /* (2) zero the whole buffer */
    memset(buf, 0xFF, sizeof buf);
    CHECK(cplus_memzero_s(buf, TOTAL) == 0);
    CHECK(all_eq(buf, 0, TOTAL, 0x00));

    /* (3) zero a single leading byte only */
    memset(buf, 0x77, sizeof buf);
    CHECK(cplus_memzero_s(buf, 1) == 0);
    CHECK(buf[0] == 0x00);
    CHECK(all_eq(buf, 1, TOTAL, 0x77));

    /* (4) a zero length is a constraint error (ESZEROL), buffer untouched */
    memset(buf, 0x5A, sizeof buf);
    CHECK(cplus_memzero_s(buf, 0) == ESZEROL);
    CHECK(all_eq(buf, 0, TOTAL, 0x5A));

    /* (5) NULL dest -> ESNULLP */
    CHECK(cplus_memzero_s(NULL, LEN) == ESNULLP);
    CHECK(cplus_memzero_s(NULL, 0) == ESNULLP);

    /* (6) len > RSIZE_MAX_MEM -> ESLEMAX, buffer untouched */
    memset(buf, 0x5A, sizeof buf);
    CHECK(cplus_memzero_s(buf, (rsize_t)RSIZE_MAX_MEM + 1) == ESLEMAX);
    CHECK(all_eq(buf, 0, TOTAL, 0x5A));

    /* (7) idempotent: zeroing an already-zero range is a no-op success */
    memset(buf, 0x00, sizeof buf);
    CHECK(cplus_memzero_s(buf, LEN) == 0);
    CHECK(all_eq(buf, 0, LEN, 0x00));

    /* (8) zeroing a middle window leaves both flanks intact */
    memset(buf, 0x3C, sizeof buf);
    CHECK(cplus_memzero_s(buf + 4, 4) == 0);
    CHECK(all_eq(buf, 0, 4, 0x3C));
    CHECK(all_eq(buf, 4, 8, 0x00));
    CHECK(all_eq(buf, 8, TOTAL, 0x3C));

    /* (9) looped stress: 100 iterations of zero-then-verify */
    for (i = 0; i < 100; i++) {
        memset(buf, (int)(i & 0xFF), sizeof buf);
        CHECK(cplus_memzero_s(buf, LEN) == 0);
        CHECK(all_eq(buf, 0, LEN, 0x00));
        CHECK(all_eq(buf, LEN, TOTAL, (unsigned char)(i & 0xFF)));
    }

    if (failures == 0) {
        printf("test_mem50_memzero: ok\n");
        return 0;
    }
    printf("test_mem50_memzero: FAILURES=%d\n", failures);
    return 1;
}
