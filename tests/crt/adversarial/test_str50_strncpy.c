/*
 * Adversarial string/format scenarios (Agent M-C, str50 series).
 *
 * cplus_strncpy_s: bounded copy where slen caps how many source bytes are
 * consumed. exact-fit vs one-past, slen shorter/longer than the source,
 * NULL arguments, empty source, dmax=0, guard bytes past dmax.
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

    /* slen shorter than source: copy exactly slen bytes + NUL */
    {
        char d[8];
        errno_t r = cplus_strncpy_s(d, 8, "abcdefg", 4);
        CHECK(r == 0);
        CHECK(strcmp(d, "abcd") == 0);
        CHECK(d[4] == '\0');
    }
    /* exact fit with slen == dmax-1 */
    {
        char d[4];
        errno_t r = cplus_strncpy_s(d, 4, "abcdefg", 3);
        CHECK(r == 0);
        CHECK(strcmp(d, "abc") == 0);
        CHECK(d[3] == '\0');
    }
    /* slen beyond capacity with more source to consume -> ESNOSPC */
    {
        char d[8];
        errno_t r = cplus_strncpy_s(d, 4, "abcdefg", 7);
        CHECK(r == ESNOSPC);
        CHECK((int)r > 0);
        CHECK(d[0] == '\0');
    }
    /* slen == dmax and source is exactly that long -> needs dmax+1 */
    {
        char d[8];
        errno_t r = cplus_strncpy_s(d, 4, "abcd", 4);
        CHECK(r == ESNOSPC);
    }
    /* slen == dmax but source is shorter -> fits */
    {
        char d[4];
        errno_t r = cplus_strncpy_s(d, 4, "abc", 4);
        CHECK(r == 0);
        CHECK(strcmp(d, "abc") == 0);
    }
    /* source shorter than slen with ample room */
    {
        char d[8];
        errno_t r = cplus_strncpy_s(d, 8, "ab", 5);
        CHECK(r == 0);
        CHECK(strcmp(d, "ab") == 0);
    }
    /* slen == strlen(source) with room */
    {
        char d[8];
        errno_t r = cplus_strncpy_s(d, 8, "abcdef", 6);
        CHECK(r == 0);
        CHECK(strcmp(d, "abcdef") == 0);
    }
    /* empty source and slen == 0 */
    {
        char a[8];
        char b[8];
        memset(a, 0x5A, sizeof a);
        memset(b, 0x5A, sizeof b);
        CHECK(cplus_strncpy_s(a, 8, "", 4) == 0);
        CHECK(a[0] == '\0');
        CHECK(cplus_strncpy_s(b, 8, "abcdefg", 0) == 0);
        CHECK(b[0] == '\0');
    }
    /* NULL arguments */
    CHECK(cplus_strncpy_s(NULL, 8, "a", 1) == ESNULLP);
    {
        char d[8];
        CHECK(cplus_strncpy_s(d, 8, NULL, 3) == ESNULLP);
    }
    /* dmax == 0 */
    {
        char d[8];
        memset(d, 0x5A, sizeof d);
        CHECK(cplus_strncpy_s(d, 0, "a", 1) == ESZEROL);
        CHECK((unsigned char)d[0] == 0x5A); /* untouched */
    }
    /* guard bytes past dmax on success */
    {
        char g[16];
        errno_t r;
        memset(g, 0x5A, sizeof g);
        r = cplus_strncpy_s(g, 8, "abcd", 4);
        CHECK(r == 0);
        CHECK(g[4] == '\0');
        CHECK((unsigned char)g[5] == 0x5A);
        CHECK((unsigned char)g[8] == 0x5A);
        CHECK((unsigned char)g[15] == 0x5A);
    }
    /* guard bytes past dmax on rejection */
    {
        char g[16];
        errno_t r;
        memset(g, 0x5A, sizeof g);
        r = cplus_strncpy_s(g, 4, "abcdefgh", 8);
        CHECK(r == ESNOSPC);
        CHECK(g[0] == '\0');
        CHECK((unsigned char)g[4] == 0x5A);
        CHECK((unsigned char)g[15] == 0x5A);
    }
    /* overlapping source and destination is undefined -> rejected */
    {
        char t[16];
        errno_t r;
        strcpy(t, "abc");
        r = cplus_strncpy_s(t, 16, t, 3);
        CHECK(r == ESOVRLP);
        CHECK(r != 0);
    }
    /* 100-iteration loops */
    {
        int i;
        int ok = 0;
        int rej = 0;
        for (i = 0; i < 100; i++) {
            char buf[8];
            if (cplus_strncpy_s(buf, 8, "abcdefg", 3) == 0 &&
                strcmp(buf, "abc") == 0)
                ok++;
        }
        CHECK(ok == 100);

        for (i = 0; i < 100; i++) {
            char buf[4];
            if (cplus_strncpy_s(buf, 4, "abcdefg", 6) == ESNOSPC)
                rej++;
        }
        CHECK(rej == 100);
    }

    if (failures == 0) {
        printf("test_str50_strncpy: ok\n");
        return 0;
    }
    printf("test_str50_strncpy: FAILURES=%d\n", failures);
    return 1;
}
