/*
 * Adversarial string/format scenarios (Agent M-C, str50 series).
 *
 * cplus_strcat_s: exact-fit vs one-past, NULL arguments, empty source,
 * unterminated destination, self-append overlap, dmax=0, guard bytes.
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

    /* exact fit: strlen("abc") + strlen("defg") + NUL == dmax 8 */
    {
        char d[16];
        errno_t r;
        strcpy(d, "abc");
        r = cplus_strcat_s(d, 8, "defg");
        CHECK(r == 0);
        CHECK(strcmp(d, "abcdefg") == 0);
        CHECK(d[7] == '\0');
    }
    /* one-past: dmax one short */
    {
        char d[16];
        errno_t r;
        strcpy(d, "abc");
        r = cplus_strcat_s(d, 7, "defg");
        CHECK(r == ESNOSPC);
        CHECK((int)r > 0);
        CHECK(d[0] == '\0'); /* rejected append clears leading byte */
    }
    /* empty source fits and preserves dest */
    {
        char d[16];
        errno_t r;
        strcpy(d, "abc");
        r = cplus_strcat_s(d, 6, "");
        CHECK(r == 0);
        CHECK(strcmp(d, "abc") == 0);
    }
    /* append onto an empty destination */
    {
        char d[8];
        errno_t r;
        d[0] = '\0';
        r = cplus_strcat_s(d, 4, "abc");
        CHECK(r == 0);
        CHECK(strcmp(d, "abc") == 0);
    }
    /* single-char dest + two-char src */
    {
        char d[8];
        errno_t r;
        strcpy(d, "x");
        r = cplus_strcat_s(d, 4, "yz");
        CHECK(r == 0);
        CHECK(strcmp(d, "xyz") == 0);
    }
    /* two successive appends build one string */
    {
        char d[16];
        d[0] = '\0';
        CHECK(cplus_strcat_s(d, 8, "abc") == 0);
        CHECK(cplus_strcat_s(d, 8, "de") == 0);
        CHECK(strcmp(d, "abcde") == 0);
    }
    /* NULL arguments */
    CHECK(cplus_strcat_s(NULL, 8, "x") == ESNULLP);
    {
        char d[8];
        strcpy(d, "a");
        CHECK(cplus_strcat_s(d, 8, NULL) == ESNULLP);
    }
    /* dmax == 0 */
    {
        char d[8];
        strcpy(d, "a");
        CHECK(cplus_strcat_s(d, 0, "b") == ESZEROL);
    }
    /* unterminated destination -> ESUNTERM (no over-read past dmax) */
    {
        char d[8];
        errno_t r;
        memset(d, 'A', sizeof d);
        r = cplus_strcat_s(d, 8, "x");
        CHECK(r == ESUNTERM);
        CHECK(d[0] == '\0'); /* leading byte cleared, rest retained */
        CHECK(d[7] == 'A');
    }
    /* self-append overlap is undefined -> rejected */
    {
        char t[16];
        errno_t r;
        strcpy(t, "abc");
        r = cplus_strcat_s(t, 16, t);
        CHECK(r == ESOVRLP);
    }
    /* guard bytes past dmax survive success and rejection */
    {
        char g[16];
        errno_t r;
        memset(g, 0x5A, sizeof g);
        memcpy(g, "abc", 4);
        r = cplus_strcat_s(g, 8, "defg");
        CHECK(r == 0);
        CHECK((unsigned char)g[8] == 0x5A);
        CHECK((unsigned char)g[15] == 0x5A);

        memset(g, 0x5A, sizeof g);
        memcpy(g, "abc", 4);
        r = cplus_strcat_s(g, 6, "defg");
        CHECK(r == ESNOSPC);
        CHECK((unsigned char)g[8] == 0x5A);
    }
    /* reject when the combined length does not fit even though each fits alone */
    {
        char d[8];
        strcpy(d, "abcd"); /* 4 chars */
        CHECK(cplus_strcat_s(d, 6, "ef") == ESNOSPC); /* 4+2+1 > 6 */
    }
    /* 100-iteration loops */
    {
        int i;
        int ok = 0;
        int rej = 0;
        for (i = 0; i < 100; i++) {
            char d[8];
            d[0] = '\0';
            if (cplus_strcat_s(d, 8, "ab") == 0 &&
                cplus_strcat_s(d, 8, "cd") == 0 && strcmp(d, "abcd") == 0)
                ok++;
        }
        CHECK(ok == 100);

        for (i = 0; i < 100; i++) {
            char d[8];
            strcpy(d, "abc");
            if (cplus_strcat_s(d, 5, "de") == ESNOSPC)
                rej++;
        }
        CHECK(rej == 100);
    }

    if (failures == 0) {
        printf("test_str50_strcat: ok\n");
        return 0;
    }
    printf("test_str50_strcat: FAILURES=%d\n", failures);
    return 1;
}
