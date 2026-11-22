/* Adversarial checked-memory scenarios (M-A, file 8/10).
 *
 * Topic: deterministic behaviour of the destination prefix when a call is
 * rejected. The copy/move APIs clear [dest, dest + dmax) on rejection; memset_s
 * performs a partial value write; some early rejections (dmax == 0 or
 * dmax > RSIZE_MAX_MEM) perform no write at all. Everything outside the prefix
 * stays intact, and repeated identical rejections are reproducible.
 *
 * Buffer is TOTAL bytes; DMAX is a truthful sub-bound. Report-only.
 */
#include <stdio.h>
#include <string.h>
#include "safe_memory.h"
#include "safe_log.h"
#include "safe_error.h"
#include "safe_lib_errno.h"

#define DMAX  8
#define TOTAL 16
#define SENT  0x5A

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
    unsigned char buf[TOTAL], src[TOTAL], again[TOTAL];
    size_t i;

    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);
    for (i = 0; i < TOTAL; i++) src[i] = (unsigned char)(0x80 + i);

    /* (1) memcpy_s NULL src clears exactly [0, DMAX), guard intact */
    memset(buf, SENT, sizeof buf);
    CHECK(cplus_memcpy_s(buf, DMAX, NULL, 4) == ESNULLP);
    CHECK(all_eq(buf, 0, DMAX, 0x00));
    CHECK(all_eq(buf, DMAX, TOTAL, SENT));

    /* (2) memcpy_s slen > dmax clears exactly [0, DMAX) */
    memset(buf, SENT, sizeof buf);
    CHECK(cplus_memcpy_s(buf, DMAX, src, DMAX + 1) == ESNOSPC);
    CHECK(all_eq(buf, 0, DMAX, 0x00));
    CHECK(all_eq(buf, DMAX, TOTAL, SENT));

    /* (3) memcpy_s overlap clears exactly [0, DMAX) */
    memset(buf, SENT, sizeof buf);
    memcpy(buf, "ABCDEFGH", DMAX);
    CHECK(cplus_memcpy_s(buf, DMAX, buf + 1, DMAX - 1) == ESOVRLP);
    CHECK(all_eq(buf, 0, DMAX, 0x00));

    /* (4) memmove_s NULL src clears exactly [0, DMAX) */
    memset(buf, SENT, sizeof buf);
    CHECK(cplus_memmove_s(buf, DMAX, NULL, 1) == ESNULLP);
    CHECK(all_eq(buf, 0, DMAX, 0x00));
    CHECK(all_eq(buf, DMAX, TOTAL, SENT));

    /* (5) memmove_s slen > dmax clears exactly [0, DMAX) */
    memset(buf, SENT, sizeof buf);
    CHECK(cplus_memmove_s(buf, DMAX, src, DMAX + 1) == ESNOSPC);
    CHECK(all_eq(buf, 0, DMAX, 0x00));
    CHECK(all_eq(buf, DMAX, TOTAL, SENT));

    /* (6) memset_s n > dmax does a partial VALUE write, not a zero clear */
    memset(buf, SENT, sizeof buf);
    CHECK(cplus_memset_s(buf, DMAX, 0x4D, DMAX + 3) == ESNOSPC);
    CHECK(all_eq(buf, 0, DMAX, 0x4D));
    CHECK(all_eq(buf, DMAX, TOTAL, SENT));

    /* (7) dmax == 0 leaves the buffer completely untouched */
    memset(buf, SENT, sizeof buf);
    CHECK(cplus_memcpy_s(buf, 0, src, 1) == ESZEROL);
    CHECK(cplus_memset_s(buf, 0, 0x33, 1) == ESNOSPC);
    CHECK(all_eq(buf, 0, TOTAL, SENT));

    /* (8) dmax > RSIZE_MAX_MEM performs no write either */
    memset(buf, SENT, sizeof buf);
    CHECK(cplus_memcpy_s(buf, (rsize_t)RSIZE_MAX_MEM + 1, src, 1) == ESLEMAX);
    CHECK(cplus_memmove_s(buf, (rsize_t)RSIZE_MAX_MEM + 1, src, 1) == ESLEMAX);
    CHECK(cplus_memset_s(buf, (rsize_t)RSIZE_MAX_MEM + 1, 0x33, 1) == ESLEMAX);
    CHECK(all_eq(buf, 0, TOTAL, SENT));

    /* (9) repeated identical rejection is byte-for-byte reproducible */
    memset(buf, SENT, sizeof buf);
    memset(again, SENT, sizeof again);
    CHECK(cplus_memcpy_s(buf, DMAX, NULL, 4) == ESNULLP);
    CHECK(cplus_memcpy_s(again, DMAX, NULL, 4) == ESNULLP);
    CHECK(memcmp(buf, again, TOTAL) == 0);

    /* (10) a subsequent valid copy overwrites the cleared prefix */
    memset(buf, SENT, sizeof buf);
    CHECK(cplus_memcpy_s(buf, DMAX, NULL, 1) == ESNULLP);
    CHECK(all_eq(buf, 0, DMAX, 0x00));
    CHECK(cplus_memcpy_s(buf, DMAX, src, DMAX) == 0);
    CHECK(memcmp(buf, src, DMAX) == 0);
    CHECK(buf[DMAX] == SENT);                /* guard still intact */

    if (failures == 0) {
        printf("test_mem50_rejected_prefix: ok\n");
        return 0;
    }
    printf("test_mem50_rejected_prefix: FAILURES=%d\n", failures);
    return 1;
}
