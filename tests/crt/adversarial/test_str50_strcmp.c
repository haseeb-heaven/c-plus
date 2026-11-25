/*
 * Adversarial string/format scenarios (Agent M-C, str50 series).
 *
 * cplus_strcmp_s: sign of the indicator, NULL dest/src/indicator, dmax=0,
 * empty strings, exact-fit dmax, and the documented byte-difference value.
 *
 * All dmax values are truthful: dmax never exceeds the real capacity of dest.
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

    /* equal strings */
    {
        int ind = 7;
        errno_t r = cplus_strcmp_s("abc", 4, "abc", &ind);
        CHECK(r == 0);
        CHECK(ind == 0);
    }
    /* dest < src -> negative indicator */
    {
        int ind = 7;
        errno_t r = cplus_strcmp_s("abc", 4, "abd", &ind);
        CHECK(r == 0);
        CHECK(ind < 0);
        CHECK(ind == -1); /* 'c' - 'd' */
    }
    /* dest > src -> positive indicator */
    {
        int ind = 7;
        errno_t r = cplus_strcmp_s("abd", 4, "abc", &ind);
        CHECK(r == 0);
        CHECK(ind > 0);
        CHECK(ind == 1);
    }
    /* prefix ordering */
    {
        int ind = 7;
        errno_t r1 = cplus_strcmp_s("abc", 4, "abcd", &ind);
        CHECK(r1 == 0);
        CHECK(ind < 0);
    }
    {
        int ind = 7;
        errno_t r2 = cplus_strcmp_s("abcd", 5, "abc", &ind);
        CHECK(r2 == 0);
        CHECK(ind > 0);
    }
    /* empty strings */
    {
        int ind = 7;
        errno_t r = cplus_strcmp_s("", 1, "", &ind);
        CHECK(r == 0);
        CHECK(ind == 0);
    }
    {
        int ind = 7;
        errno_t r = cplus_strcmp_s("", 1, "a", &ind);
        CHECK(r == 0);
        CHECK(ind < 0);
    }
    {
        int ind = 7;
        errno_t r = cplus_strcmp_s("a", 2, "", &ind);
        CHECK(r == 0);
        CHECK(ind > 0);
    }
    /* case sensitivity */
    {
        int ind = 7;
        errno_t r = cplus_strcmp_s("abc", 4, "ABC", &ind);
        CHECK(r == 0);
        CHECK(ind > 0); /* 'a' > 'A' */
    }
    /* single-char difference value */
    {
        int ind = 7;
        errno_t r = cplus_strcmp_s("b", 2, "a", &ind);
        CHECK(r == 0);
        CHECK(ind == 1);
    }
    /* NULL arguments */
    {
        int ind = 7;
        errno_t r = cplus_strcmp_s("abc", 4, "abc", NULL);
        CHECK(r == ESNULLP);
        CHECK(r != 0);
    }
    {
        int ind = 7;
        errno_t r = cplus_strcmp_s(NULL, 4, "abc", &ind);
        CHECK(r == ESNULLP);
    }
    {
        int ind = 7;
        errno_t r = cplus_strcmp_s("abc", 4, NULL, &ind);
        CHECK(r == ESNULLP);
    }
    /* dmax == 0 */
    {
        int ind = 7;
        errno_t r = cplus_strcmp_s("abc", 0, "abc", &ind);
        CHECK(r == ESZEROL);
        CHECK(r != 0);
    }
    /* comparing two separate equal buffers */
    {
        char a[8];
        char b[8];
        int ind = 7;
        errno_t r;
        strcpy(a, "same");
        strcpy(b, "same");
        r = cplus_strcmp_s(a, 5, b, &ind);
        CHECK(r == 0);
        CHECK(ind == 0);
    }
    /* 100-iteration loops: equal and ordered comparisons stay stable */
    {
        int i;
        int eq = 0;
        int lt = 0;
        int gt = 0;
        for (i = 0; i < 100; i++) {
            int ind = 7;
            if (cplus_strcmp_s("loop", 5, "loop", &ind) == 0 && ind == 0)
                eq++;
            ind = 7;
            if (cplus_strcmp_s("loop", 5, "loox", &ind) == 0 && ind < 0)
                lt++;
            ind = 7;
            if (cplus_strcmp_s("looz", 5, "loop", &ind) == 0 && ind > 0)
                gt++;
        }
        CHECK(eq == 100);
        CHECK(lt == 100);
        CHECK(gt == 100);
    }

    if (failures == 0) {
        printf("test_str50_strcmp: ok\n");
        return 0;
    }
    printf("test_str50_strcmp: FAILURES=%d\n", failures);
    return 1;
}
