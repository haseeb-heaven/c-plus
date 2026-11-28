/*
 * Adversarial string/format scenarios (Agent M-C, str50 series).
 *
 * cplus_strcpy_s: exact-fit vs one-past, NULL arguments, empty source, dmax=0,
 * rejected-copy destination retention, and guard bytes past dmax.
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

    /* exact fit: "abc" + NUL == dmax */
    {
        char d[8];
        errno_t r = cplus_strcpy_s(d, 4, "abc");
        CHECK(r == 0);
        CHECK(strcmp(d, "abc") == 0);
        CHECK(d[3] == '\0');
    }
    /* exact fit at a larger width: 7 chars + NUL == dmax 8 */
    {
        char d[8];
        errno_t r = cplus_strcpy_s(d, 8, "abcdefg");
        CHECK(r == 0);
        CHECK(strcmp(d, "abcdefg") == 0);
        CHECK(d[7] == '\0');
    }
    /* one-past: source needs dmax+1 */
    {
        char d[8];
        errno_t r = cplus_strcpy_s(d, 3, "abc");
        CHECK(r == ESNOSPC);
        CHECK(r != 0);
        CHECK((int)r > 0);
        CHECK(d[0] == '\0'); /* rejected copy: leading byte cleared */
    }
    {
        char d[8];
        errno_t r = cplus_strcpy_s(d, 7, "abcdefg");
        CHECK(r == ESNOSPC);
    }
    /* NULL arguments */
    {
        char d[8];
        CHECK(cplus_strcpy_s(NULL, 8, "x") == ESNULLP);
        CHECK(cplus_strcpy_s(d, 8, NULL) == ESNULLP);
        CHECK(cplus_strcpy_s(NULL, 8, NULL) == ESNULLP);
    }
    /* dmax == 0 */
    {
        char d[8];
        CHECK(cplus_strcpy_s(d, 0, "x") == ESZEROL);
    }
    /* empty source */
    {
        char e1[1];
        char e2[8];
        errno_t r1 = cplus_strcpy_s(e1, 1, "");
        errno_t r2 = cplus_strcpy_s(e2, 8, "");
        CHECK(r1 == 0);
        CHECK(e1[0] == '\0');
        CHECK(r2 == 0);
        CHECK(e2[0] == '\0');
    }
    /* guard bytes past dmax must survive both success and rejection */
    {
        char g[16];
        errno_t r;
        memset(g, 0x5A, sizeof g);
        r = cplus_strcpy_s(g, 4, "abc");
        CHECK(r == 0);
        CHECK(g[3] == '\0');
        CHECK((unsigned char)g[4] == 0x5A);
        CHECK((unsigned char)g[15] == 0x5A);

        memset(g, 0x5A, sizeof g);
        r = cplus_strcpy_s(g, 3, "abcdef");
        CHECK(r == ESNOSPC);
        CHECK(g[0] == '\0');
        CHECK((unsigned char)g[3] == 0x5A);
        CHECK((unsigned char)g[15] == 0x5A);
    }
    /* self copy (dest == src) is a defined no-op */
    {
        char t[16];
        errno_t r;
        strcpy(t, "abc");
        r = cplus_strcpy_s(t, 16, t);
        CHECK(r == 0);
        CHECK(strcmp(t, "abc") == 0);
    }
    /* 100-iteration loops: stable successes and stable rejections */
    {
        int i;
        int ok = 0;
        int rej = 0;
        for (i = 0; i < 100; i++) {
            char buf[8];
            if (cplus_strcpy_s(buf, 8, "Zz9") == 0 && strcmp(buf, "Zz9") == 0)
                ok++;
        }
        CHECK(ok == 100);

        for (i = 0; i < 100; i++) {
            char buf[4];
            if (cplus_strcpy_s(buf, 3, "toolong") == ESNOSPC)
                rej++;
        }
        CHECK(rej == 100);
    }

    if (failures == 0) {
        printf("test_str50_strcpy: ok\n");
        return 0;
    }
    printf("test_str50_strcpy: FAILURES=%d\n", failures);
    return 1;
}
