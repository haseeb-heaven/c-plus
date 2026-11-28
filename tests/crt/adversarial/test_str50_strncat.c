/*
 * Adversarial string/format scenarios (Agent M-C, str50 series).
 *
 * cplus_strncat_s: bounded append. exact-fit vs one-past, slen shorter/longer
 * than the source, empty source, NULL arguments, unterminated destination,
 * self-append overlap, dmax=0, guard bytes.
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

    /* slen caps the append at 2 source bytes */
    {
        char d[16];
        errno_t r;
        strcpy(d, "abc");
        r = cplus_strncat_s(d, 8, "defgh", 2);
        CHECK(r == 0);
        CHECK(strcmp(d, "abcde") == 0);
        CHECK(d[5] == '\0');
    }
    /* source shorter than slen: the whole source is appended */
    {
        char d[16];
        errno_t r;
        strcpy(d, "abc");
        r = cplus_strncat_s(d, 8, "de", 5);
        CHECK(r == 0);
        CHECK(strcmp(d, "abcde") == 0);
    }
    /* slen larger than the source with ample room */
    {
        char d[16];
        errno_t r;
        strcpy(d, "abc");
        r = cplus_strncat_s(d, 7, "de", 5);
        CHECK(r == 0);
        CHECK(strcmp(d, "abcde") == 0);
    }
    /* exact fit: strlen("abc") + 2 + NUL == dmax 6 */
    {
        char d[16];
        errno_t r;
        strcpy(d, "abc");
        r = cplus_strncat_s(d, 6, "de", 2);
        CHECK(r == 0);
        CHECK(strcmp(d, "abcde") == 0);
        CHECK(d[5] == '\0');
    }
    /* one-past: same append but dmax one short */
    {
        char d[16];
        errno_t r;
        strcpy(d, "abc");
        r = cplus_strncat_s(d, 5, "de", 2);
        CHECK(r == ESNOSPC);
        CHECK((int)r > 0);
        CHECK(d[0] == '\0');
    }
    /* slen far larger than dmax -> rejected */
    {
        char d[16];
        strcpy(d, "abc");
        CHECK(cplus_strncat_s(d, 8, "defgh", 40) == ESNOSPC);
    }
    /* slen == 0 (documented no-op; dest is normalised to empty) */
    {
        char d[16];
        errno_t r;
        strcpy(d, "abc");
        r = cplus_strncat_s(d, 8, "xyz", 0);
        CHECK(r == 0);
        CHECK(d[0] == '\0');
    }
    /* NULL arguments */
    {
        char d[16];
        strcpy(d, "a");
        CHECK(cplus_strncat_s(d, 8, NULL, 2) == ESNULLP);
        CHECK(cplus_strncat_s(NULL, 8, "a", 1) == ESNULLP);
    }
    /* dmax == 0 */
    {
        char d[8];
        strcpy(d, "a");
        CHECK(cplus_strncat_s(d, 0, "b", 1) == ESZEROL);
    }
    /* unterminated destination -> ESUNTERM */
    {
        char d[8];
        errno_t r;
        memset(d, 'A', sizeof d);
        r = cplus_strncat_s(d, 8, "x", 1);
        CHECK(r == ESUNTERM);
        CHECK(d[0] == '\0');
        CHECK(d[7] == 'A');
    }
    /* self-append overlap is undefined -> rejected */
    {
        char t[16];
        errno_t r;
        strcpy(t, "abc");
        r = cplus_strncat_s(t, 16, t, 3);
        CHECK(r == ESOVRLP);
    }
    /* guard bytes past dmax survive success and rejection */
    {
        char g[16];
        errno_t r;
        memset(g, 0x5A, sizeof g);
        memcpy(g, "abc", 4);
        r = cplus_strncat_s(g, 8, "defgh", 2);
        CHECK(r == 0);
        CHECK((unsigned char)g[8] == 0x5A);
        CHECK((unsigned char)g[15] == 0x5A);

        memset(g, 0x5A, sizeof g);
        memcpy(g, "abc", 4);
        r = cplus_strncat_s(g, 5, "defgh", 3);
        CHECK(r == ESNOSPC);
        CHECK((unsigned char)g[8] == 0x5A);
    }
    /* 100-iteration loops */
    {
        int i;
        int ok = 0;
        int rej = 0;
        for (i = 0; i < 100; i++) {
            char d[8];
            strcpy(d, "a");
            if (cplus_strncat_s(d, 8, "bcdef", 2) == 0 &&
                strcmp(d, "abc") == 0)
                ok++;
        }
        CHECK(ok == 100);

        for (i = 0; i < 100; i++) {
            char d[8];
            strcpy(d, "abc");
            if (cplus_strncat_s(d, 5, "de", 2) == ESNOSPC)
                rej++;
        }
        CHECK(rej == 100);
    }

    if (failures == 0) {
        printf("test_str50_strncat: ok\n");
        return 0;
    }
    printf("test_str50_strncat: FAILURES=%d\n", failures);
    return 1;
}
