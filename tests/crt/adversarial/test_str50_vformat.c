/*
 * Adversarial string/format scenarios (Agent M-C, str50 series).
 *
 * cplus_vsnprintf_s: the va_list form of the bounded formatter. Truncation
 * returns the would-be length (>= dmax), output is NUL-terminated and never
 * exceeds dmax; invalid arguments return -1 with EINVAL. Cross-checked against
 * the variadic cplus_snprintf_s for identical results.
 *
 * All dmax values are truthful: dmax never exceeds the real capacity of dest.
 */
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
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

static int vwrap(char *d, size_t n, const char *f, ...) {
    va_list ap;
    int r;
    va_start(ap, f);
    r = cplus_vsnprintf_s(d, n, f, ap);
    va_end(ap);
    return r;
}

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);

    /* normal formatting through the va_list form */
    {
        char f[64];
        int n = vwrap(f, 64, "%d-%s", 42, "xy");
        CHECK(n == 5);
        CHECK(strcmp(f, "42-xy") == 0);
    }
    /* exact fit */
    {
        char f[6];
        int n = vwrap(f, 6, "%d-%s", 42, "xy");
        CHECK(n == 5);
        CHECK(strcmp(f, "42-xy") == 0);
    }
    /* truncation: would-be length, bounded, NUL-terminated */
    {
        char f[4];
        int n;
        memset(f, 0x5A, sizeof f);
        n = vwrap(f, 4, "%s", "abcdefgh");
        CHECK(n == 8);
        CHECK(n >= (int)4);
        CHECK(strcmp(f, "abc") == 0);
        CHECK(f[3] == '\0');
    }
    /* dmax == 1 */
    {
        char f[1];
        int n;
        f[0] = '#';
        n = vwrap(f, 1, "%s", "abcdef");
        CHECK(n == 6);
        CHECK(f[0] == '\0');
    }
    /* dmax == 0 writes nothing */
    {
        char f[8];
        int n;
        memset(f, 0x5A, sizeof f);
        n = vwrap(f, 0, "%s", "abc");
        CHECK(n == 3);
        CHECK((unsigned char)f[0] == 0x5A);
        CHECK((unsigned char)f[7] == 0x5A);
    }
    /* dest == NULL, dmax == 0 is a valid sizing query */
    {
        int n = vwrap(NULL, 0, "%s", "abc");
        CHECK(n == 3);
        CHECK(n >= 0);
    }
    /* dest == NULL, dmax != 0 is invalid */
    {
        int n;
        errno = 0;
        n = vwrap(NULL, 4, "%s", "abc");
        CHECK(n == -1);
        CHECK(errno == EINVAL);
    }
    /* NULL format is invalid */
    {
        char f[8];
        int n;
        errno = 0;
        n = vwrap(f, 8, NULL);
        CHECK(n == -1);
        CHECK(errno == EINVAL);
    }
    /* integer truncation */
    {
        char f[8];
        int n;
        memset(f, 0x5A, sizeof f);
        n = vwrap(f, 8, "%d", 1234567890);
        CHECK(n == 10);
        CHECK(strcmp(f, "1234567") == 0);
        CHECK(f[7] == '\0');
    }
    /* assorted conversions */
    {
        char f[16];
        CHECK(vwrap(f, 16, "%3d", 5) == 3);
        CHECK(strcmp(f, "  5") == 0);
        CHECK(vwrap(f, 16, "%d", -42) == 3);
        CHECK(strcmp(f, "-42") == 0);
        CHECK(vwrap(f, 16, "%x", 255) == 2);
        CHECK(strcmp(f, "ff") == 0);
        CHECK(vwrap(f, 16, "%c", 'Q') == 1);
        CHECK(strcmp(f, "Q") == 0);
    }
    /* empty format writes an empty string */
    {
        char f[8];
        memset(f, 0x5A, sizeof f);
        CHECK(vwrap(f, 8, "") == 0);
        CHECK(f[0] == '\0');
    }
    /* cross-check: variadic and va_list forms agree */
    {
        char a[16];
        char b[16];
        int na = cplus_snprintf_s(a, 16, "%d/%s", 7, "x");
        int nb = vwrap(b, 16, "%d/%s", 7, "x");
        CHECK(na == nb);
        CHECK(strcmp(a, b) == 0);
    }
    /* guard bytes past dmax survive truncation */
    {
        char g[16];
        int n;
        memset(g, 0x5A, sizeof g);
        n = vwrap(g, 4, "%s", "abcdefgh");
        CHECK(n == 8);
        CHECK(g[3] == '\0');
        CHECK((unsigned char)g[4] == 0x5A);
        CHECK((unsigned char)g[15] == 0x5A);
    }
    /* 100-iteration loops: stable exact and truncating behaviour */
    {
        int i;
        int exact = 0;
        int trunc = 0;
        for (i = 0; i < 100; i++) {
            char f[8];
            if (vwrap(f, 8, "%s", "abc") == 3 && strcmp(f, "abc") == 0)
                exact++;
        }
        CHECK(exact == 100);

        for (i = 0; i < 100; i++) {
            char f[4];
            size_t len;
            int n = vwrap(f, 4, "%s", "0123456789");
            len = strlen(f);
            if (n == 10 && len < 4 && f[len] == '\0')
                trunc++;
        }
        CHECK(trunc == 100);
    }

    if (failures == 0) {
        printf("test_str50_vformat: ok\n");
        return 0;
    }
    printf("test_str50_vformat: FAILURES=%d\n", failures);
    return 1;
}
