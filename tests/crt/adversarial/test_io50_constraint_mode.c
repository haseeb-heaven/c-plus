/*
 * Adversarial io50 #5: cplus_safe_set_constraint_mode / _get_constraint_mode.
 *
 * Raw-C trap: an out-of-range enum value silently selecting an undefined
 * handler. The setter must accept only IGNORE/ABORT and fall back to IGNORE for
 * anything else; the getter must reflect the current mode. Under IGNORE a real
 * constraint violation (an over-long source) must return an error and let the
 * program continue -- it never aborts or silently corrupts.
 */
#include <stdio.h>
#include <string.h>
#include "safe_error.h"
#include "safe_io.h"
#include "safe_log.h"
#include "safe_memory.h"   /* for a genuine constraint violation */

static int failures = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            failures++;                                                        \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__,   \
                    #cond);                                                    \
        }                                                                      \
    } while (0)

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);

    /* --- round-trip both valid modes --- */
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);
    CHECK(cplus_safe_get_constraint_mode() == CPLUS_SAFE_CONSTRAINT_IGNORE);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_ABORT);
    CHECK(cplus_safe_get_constraint_mode() == CPLUS_SAFE_CONSTRAINT_ABORT);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);
    CHECK(cplus_safe_get_constraint_mode() == CPLUS_SAFE_CONSTRAINT_IGNORE);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_ABORT);
    CHECK(cplus_safe_get_constraint_mode() == CPLUS_SAFE_CONSTRAINT_ABORT);
    CHECK(cplus_safe_get_constraint_mode() == CPLUS_SAFE_CONSTRAINT_ABORT);
    CHECK(cplus_safe_get_constraint_mode() == cplus_safe_get_constraint_mode());

    /* --- invalid values fall back to IGNORE --- */
    cplus_safe_set_constraint_mode((cplus_safe_constraint_mode)2);
    CHECK(cplus_safe_get_constraint_mode() == CPLUS_SAFE_CONSTRAINT_IGNORE);
    cplus_safe_set_constraint_mode((cplus_safe_constraint_mode)7);
    CHECK(cplus_safe_get_constraint_mode() == CPLUS_SAFE_CONSTRAINT_IGNORE);
    cplus_safe_set_constraint_mode((cplus_safe_constraint_mode)-1);
    CHECK(cplus_safe_get_constraint_mode() == CPLUS_SAFE_CONSTRAINT_IGNORE);
    cplus_safe_set_constraint_mode((cplus_safe_constraint_mode)99);
    CHECK(cplus_safe_get_constraint_mode() == CPLUS_SAFE_CONSTRAINT_IGNORE);
    CHECK(cplus_safe_get_constraint_mode() == CPLUS_SAFE_CONSTRAINT_IGNORE);

    /* setting ABORT and then an invalid value still lands on IGNORE */
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_ABORT);
    CHECK(cplus_safe_get_constraint_mode() == CPLUS_SAFE_CONSTRAINT_ABORT);
    cplus_safe_set_constraint_mode((cplus_safe_constraint_mode)12345);
    CHECK(cplus_safe_get_constraint_mode() == CPLUS_SAFE_CONSTRAINT_IGNORE);

    /* --- under IGNORE a violation returns an error and execution continues --- */
    CHECK(cplus_safe_get_constraint_mode() == CPLUS_SAFE_CONSTRAINT_IGNORE);
    {
        char dst[8];
        int i;
        for (i = 0; i < 8; ++i) dst[i] = (char)0x33;
        /* src bound (4) exceeds dst bound (2): a constraint violation */
        CHECK(cplus_memcpy_s(dst, 2, "abcd", 4) != 0);            /* error */
        CHECK(cplus_safe_get_constraint_mode() == CPLUS_SAFE_CONSTRAINT_IGNORE);
        dst[0] = 'Z';                                             /* continue */
        CHECK(dst[0] == 'Z');
        CHECK(cplus_memcpy_s(dst, sizeof dst, "ok", 2) == 0);     /* still works */
        CHECK(memcmp(dst, "ok", 2) == 0);
    }

    /* --- after ABORT is deselected the same violation does not abort --- */
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_ABORT);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);
    {
        char b[4];
        CHECK(cplus_memcpy_s(b, 1, "abcd", 4) != 0);
        CHECK(1);   /* reaching here proves the process did not abort */
    }

    /* --- the io adapters keep their error contract under a set mode --- */
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);
    CHECK(cplus_safe_get_constraint_mode() == CPLUS_SAFE_CONSTRAINT_IGNORE);
    {
        CHECK(cplus_fopen_s(NULL, "wb") == NULL);
        FILE *f = cplus_fopen_s("/tmp/cplus_io50_cmode.tmp", "wb");
        CHECK(f != NULL);
        CHECK(cplus_fclose_s(f) == 0);
        remove("/tmp/cplus_io50_cmode.tmp");
    }

    if (failures == 0) {
        printf("test_io50_constraint_mode: ok\n");
        return 0;
    }
    printf("test_io50_constraint_mode: FAILURES=%d\n", failures);
    return 1;
}
