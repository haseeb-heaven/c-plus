/*
 * Adversarial string/format scenarios (Agent M-C, str50 series).
 *
 * cplus_strnlen_s: a bounded length query that must never read past smax and
 * must return min(strlen(str), smax). Tested against terminated and
 * non-terminated buffers, NULL, empty strings and boundary smax values.
 */
#include <stdio.h>
#include <string.h>
#include "safe_string.h"
#include "safe_error.h"
#include "safe_log.h"
#include "safe_lib_errno.h"

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
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);

    /* NUL found within smax -> real length */
    CHECK(cplus_strnlen_s("abc", 8) == (rsize_t)3);
    CHECK(cplus_strnlen_s("abc", 3) == (rsize_t)3);
    /* smax caps the answer */
    CHECK(cplus_strnlen_s("abc", 2) == (rsize_t)2);
    CHECK(cplus_strnlen_s("abc", 1) == (rsize_t)1);
    CHECK(cplus_strnlen_s("abc", 0) == (rsize_t)0);
    /* NULL is defined as length 0, with or without a bound */
    CHECK(cplus_strnlen_s(NULL, 8) == (rsize_t)0);
    CHECK(cplus_strnlen_s(NULL, 0) == (rsize_t)0);
    /* empty strings */
    CHECK(cplus_strnlen_s("", 8) == (rsize_t)0);
    CHECK(cplus_strnlen_s("", 0) == (rsize_t)0);
    /* one-char string */
    CHECK(cplus_strnlen_s("Z", 1) == (rsize_t)1);
    CHECK(cplus_strnlen_s("Z", 4) == (rsize_t)1);

    /* non-terminated buffer: returns smax, never over-reads */
    {
        char buf[8];
        rsize_t n;
        memset(buf, 'A', sizeof buf); /* no NUL anywhere */
        n = cplus_strnlen_s(buf, sizeof buf);
        CHECK(n == (rsize_t)8);
        CHECK(n <= (rsize_t)sizeof buf);
        CHECK(cplus_strnlen_s(buf, 4) == (rsize_t)4);
        CHECK(cplus_strnlen_s(buf, 1) == (rsize_t)1);
    }
    /* NUL at a known interior offset */
    {
        char buf[8];
        rsize_t n;
        memset(buf, 'B', sizeof buf);
        buf[5] = '\0';
        n = cplus_strnlen_s(buf, sizeof buf);
        CHECK(n == (rsize_t)5);
        CHECK(cplus_strnlen_s(buf, 5) == (rsize_t)5);
        CHECK(cplus_strnlen_s(buf, 4) == (rsize_t)4); /* bound wins */
        CHECK(cplus_strnlen_s(buf, 6) == (rsize_t)5); /* NUL found */
    }
    /* a long non-terminated region beyond any single read */
    {
        char big[100];
        rsize_t n;
        memset(big, 'C', sizeof big);
        n = cplus_strnlen_s(big, sizeof big);
        CHECK(n == (rsize_t)100);
        CHECK(cplus_strnlen_s(big, 50) == (rsize_t)50);
        CHECK(cplus_strnlen_s(big, 1) == (rsize_t)1);
    }
    /* 100-iteration sweeps over every smax value */
    {
        int i;
        int exact = 0;
        for (i = 0; i < 100; i++) {
            rsize_t n = cplus_strnlen_s("abc", (rsize_t)i);
            rsize_t want = ((rsize_t)i < (rsize_t)3) ? (rsize_t)i : (rsize_t)3;
            if (n == want)
                exact++;
        }
        CHECK(exact == 100);
    }
    /* invariant sweep: result never exceeds the bound */
    {
        int i;
        int invariant = 0;
        char hi[64];
        memset(hi, 'D', sizeof hi);
        for (i = 0; i < 100; i++) {
            rsize_t bound = (rsize_t)(i % 64);
            rsize_t n = cplus_strnlen_s(hi, bound);
            if (n <= bound)
                invariant++;
        }
        CHECK(invariant == 100);
    }

    if (failures == 0) {
        printf("test_str50_strnlen: ok\n");
        return 0;
    }
    printf("test_str50_strnlen: FAILURES=%d\n", failures);
    return 1;
}
