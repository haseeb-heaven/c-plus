/*
 * Bug CLASS: constraint-handler misconfiguration that either aborts on a
 * recoverable violation or silently fails to report one.
 *
 * The checked C+ safe API defaults to IGNORE mode: a runtime-constraint
 * violation (NULL pointer, bad length) is reported through the return value and
 * execution CONTINUES -- it does not abort. The mode is queryable and settable,
 * and an invalid mode value must fall back to the safe IGNORE default rather
 * than leaving a process-fatal handler installed.
 *
 * This test never triggers a violation while ABORT is selected, so the process
 * must reach the end normally.
 */
#include <stdio.h>
#include <string.h>
#include "safe_memory.h"
#include "safe_error.h"
#include "safe_log.h"
#include "safe_lib_errno.h" /* ESNULLP */

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

    /* --- default is IGNORE, before anything has been configured --- */
    CHECK(cplus_safe_get_constraint_mode() == CPLUS_SAFE_CONSTRAINT_IGNORE);

    /* --- IGNORE keeps executing after a violation: error returned, no abort - */
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);
    CHECK(cplus_safe_get_constraint_mode() == CPLUS_SAFE_CONSTRAINT_IGNORE);
    {
        char d[8];
        errno_t r = cplus_memcpy_s(NULL, sizeof d, "x", 1);
        CHECK(r != 0);                    /* a defined error, not a crash     */
        CHECK(r == ESNULLP);
        /* the very next statements run -- proof the process did not abort */
        CHECK(cplus_memcpy_s(d, sizeof d, "ok", 2) == 0);
        CHECK(memcmp(d, "ok", 2) == 0);
        CHECK(cplus_memcpy_s(NULL, 0, NULL, 1) != 0);       /* repeat violation */
        CHECK(cplus_memset_s(NULL, sizeof d, 0, 4) == ESNULLP);
    }

    /* --- mode round-trips through set/get --- */
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_ABORT);
    CHECK(cplus_safe_get_constraint_mode() == CPLUS_SAFE_CONSTRAINT_ABORT);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);
    CHECK(cplus_safe_get_constraint_mode() == CPLUS_SAFE_CONSTRAINT_IGNORE);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_ABORT);
    CHECK(cplus_safe_get_constraint_mode() == CPLUS_SAFE_CONSTRAINT_ABORT);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);
    CHECK(cplus_safe_get_constraint_mode() == CPLUS_SAFE_CONSTRAINT_IGNORE);

    /* --- invalid mode falls back to IGNORE (no abort handler installed) --- */
    {
        int bad = 99;
        cplus_safe_set_constraint_mode((cplus_safe_constraint_mode)bad);
        CHECK(cplus_safe_get_constraint_mode() == CPLUS_SAFE_CONSTRAINT_IGNORE);
    }
    {
        int negative = -7;
        cplus_safe_set_constraint_mode((cplus_safe_constraint_mode)negative);
        CHECK(cplus_safe_get_constraint_mode() == CPLUS_SAFE_CONSTRAINT_IGNORE);
    }

    /* --- under the fallback IGNORE mode, a violation is still survivable --- */
    {
        char d[4];
        memset(d, 0, sizeof d);
        CHECK(cplus_memcpy_s(NULL, 4, "y", 1) != 0);
        CHECK(cplus_memcpy_s(d, sizeof d, "z", 1) == 0);
        CHECK(d[0] == 'z');               /* still executing normally */
    }

    if (failures == 0) {
        printf("bugclass2_constraint_handler: ok\n");
        return 0;
    }
    printf("bugclass2_constraint_handler: FAILURES=%d\n", failures);
    return 1;
}
