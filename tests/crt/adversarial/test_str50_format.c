/*
 * Adversarial string/format scenarios (Agent M-C, str50 series).
 *
 * cplus_snprintf_s / cplus_sprintf_s: bounded C99-style formatting. Truncation
 * returns the would-be length (>= dmax), the result is always NUL-terminated
 * and never writes past dmax; invalid arguments return -1 with EINVAL.
 *
 * All dmax values are truthful: dmax never exceeds the real capacity of dest.
 */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "safe_format.h"
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

    /* normal formatting */
    {
        char f[64];
        int n = cplus_snprintf_s(f, 64, "%d-%s", 42, "xy");
        CHECK(n == 5);
        CHECK(strcmp(f, "42-xy") == 0);
        CHECK(n == (int)strlen(f));
    }
    /* exact fit: 5 chars + NUL == dmax 6 */
    {
        char f[6];
        int n = cplus_snprintf_s(f, 6, "%d-%s", 42, "xy");
        CHECK(n == 5);
        CHECK(strcmp(f, "42-xy") == 0);
        CHECK(f[5] == '\0');
    }
    /* truncation: returns the would-be length, dest holds dmax-1 chars */
    {
        char f[4];
        int n;
        memset(f, 0x5A, sizeof f);
        n = cplus_snprintf_s(f, 4, "%s", "abcdefgh");
        CHECK(n == 8);           /* would-be >= dmax */
        CHECK(n >= (int)4);
        CHECK(strcmp(f, "abc") == 0);
        CHECK(f[3] == '\0');     /* always NUL-terminated within dmax */
        CHECK(memchr(f, '\0', 4) != NULL);
    }
    /* dmax == 1 writes only the terminator */
    {
        char f[1];
        int n;
        f[0] = '#';
        n = cplus_snprintf_s(f, 1, "%s", "abcdef");
        CHECK(n == 6);
        CHECK(f[0] == '\0');
    }
    /* dmax == 0 writes nothing but still reports the would-be length */
    {
        char f[8];
        int n;
        memset(f, 0x5A, sizeof f);
        n = cplus_snprintf_s(f, 0, "%s", "abc");
        CHECK(n == 3);
        CHECK((unsigned char)f[0] == 0x5A);
        CHECK((unsigned char)f[7] == 0x5A);
    }
    /* dest == NULL with dmax == 0 is the "sizing" query (C99 semantics) */
    {
        int n = cplus_snprintf_s(NULL, 0, "%s", "abc");
        CHECK(n == 3);
        CHECK(n >= 0);
    }
    /* dest == NULL with dmax != 0 is invalid */
    {
        int n;
        errno = 0;
        n = cplus_snprintf_s(NULL, 4, "%s", "abc");
        CHECK(n == -1);
        CHECK(n < 0);
        CHECK(errno == EINVAL);
    }
    /* NULL format is invalid */
    {
        char f[8];
        int n;
        memset(f, 0x5A, sizeof f);
        errno = 0;
        n = cplus_snprintf_s(f, 8, NULL);
        CHECK(n == -1);
        CHECK(errno == EINVAL);
        CHECK((unsigned char)f[0] == 0x5A);
    }
    /* integer truncation */
    {
        char f[8];
        int n;
        memset(f, 0x5A, sizeof f);
        n = cplus_snprintf_s(f, 8, "%d", 1234567890);
        CHECK(n == 10);
        CHECK(n >= (int)8);
        CHECK(strcmp(f, "1234567") == 0);
        CHECK(f[7] == '\0');
    }
    /* width padding is respected when it fits */
    {
        char f[8];
        int n = cplus_snprintf_s(f, 8, "%3d", 5);
        CHECK(n == 3);
        CHECK(strcmp(f, "  5") == 0);
    }
    /* sprintf_s is a bounded alias, not Annex K reject-on-overflow */
    {
        char f[8];
        int n = cplus_sprintf_s(f, 8, "%d", 7);
        CHECK(n == 1);
        CHECK(strcmp(f, "7") == 0);
    }
    {
        char f[8];
        int n;
        memset(f, 0x5A, sizeof f);
        n = cplus_sprintf_s(f, 8, "%s", "abcdefghij");
        CHECK(n == 10);          /* would-be length, not an error */
        CHECK(strcmp(f, "abcdefg") == 0);
        CHECK(f[7] == '\0');
    }
    {
        int n;
        errno = 0;
        n = cplus_sprintf_s(NULL, 8, "%d", 1);
        CHECK(n == -1);
        CHECK(errno == EINVAL);
    }
    {
        char f[8];
        int n;
        errno = 0;
        n = cplus_sprintf_s(f, 8, NULL);
        CHECK(n == -1);
        CHECK(errno == EINVAL);
    }
    /* guard bytes past dmax survive truncation */
    {
        char g[16];
        int n;
        memset(g, 0x5A, sizeof g);
        n = cplus_snprintf_s(g, 4, "%s", "abcdefgh");
        CHECK(n == 8);
        CHECK(g[3] == '\0');
        CHECK((unsigned char)g[4] == 0x5A);
        CHECK((unsigned char)g[15] == 0x5A);
    }
    /* 100-iteration loops: exact and truncating calls */
    {
        int i;
        int exact = 0;
        int trunc = 0;
        for (i = 0; i < 100; i++) {
            char f[8];
            int n = cplus_snprintf_s(f, 8, "%s", "abc");
            if (n == 3 && strcmp(f, "abc") == 0)
                exact++;
        }
        CHECK(exact == 100);

        for (i = 0; i < 100; i++) {
            char f[4];
            size_t len;
            int n = cplus_snprintf_s(f, 4, "%s", "0123456789");
            len = strlen(f);
            if (n == 10 && len < 4 && f[len] == '\0')
                trunc++;
        }
        CHECK(trunc == 100);
    }

    if (failures == 0) {
        printf("test_str50_format: ok\n");
        return 0;
    }
    printf("test_str50_format: FAILURES=%d\n", failures);
    return 1;
}
