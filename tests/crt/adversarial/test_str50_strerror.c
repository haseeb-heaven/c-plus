/*
 * Adversarial string/format scenarios (Agent M-C, str50 series).
 *
 * cplus_strerror_s: the normal path, tiny buffers (ESLEMIN), and the "..."
 * truncation marker. Unlike the format family, strerror_s returns 0 (EOK) even
 * when it truncates, and only fails when dmax is too small to hold any marker.
 *
 * All dmax values are truthful: dmax never exceeds the real capacity of dest.
 */
#include <stdio.h>
#include <string.h>
#include <errno.h>
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

    /* normal path: ample buffer holds the whole message, no truncation mark */
    {
        char e[64];
        errno_t r = cplus_strerror_s(e, 64, ESNULLP);
        CHECK(r == 0);
        CHECK(strlen(e) > 0);
        CHECK(strstr(e, "...") == NULL);
        CHECK(strcmp(e, "null ptr") == 0);
    }
    /* a second known message with a different length */
    {
        char e[64];
        errno_t r = cplus_strerror_s(e, 64, ESZEROL);
        CHECK(r == 0);
        CHECK(strcmp(e, "length is zero") == 0);
    }
    /* a system errno is rendered non-empty and NUL-terminated */
    {
        char e[128];
        errno_t r = cplus_strerror_s(e, 128, EINVAL);
        CHECK(r == 0);
        CHECK(strlen(e) > 0);
        CHECK(e[0] != '\0');
    }
    /* exact fit: dmax == strlen(message)+1 keeps the full message */
    {
        char e[16];
        errno_t r = cplus_strerror_s(e, 9, ESNULLP); /* "null ptr" is 8 + NUL */
        CHECK(r == 0);
        CHECK(strcmp(e, "null ptr") == 0);
    }
    /* truncation marker "..." when dmax is one short of the message */
    {
        char e[16];
        errno_t r = cplus_strerror_s(e, 8, ESNULLP); /* len 8 >= dmax 8 */
        CHECK(r == 0);
        CHECK(strstr(e, "...") != NULL);
        CHECK(strlen(e) == 7); /* dmax-4 chars + "..." */
        CHECK(strncmp(e, "null", 4) == 0);
        CHECK(e[4] == '.' && e[5] == '.' && e[6] == '.');
    }
    {
        char e[16];
        errno_t r = cplus_strerror_s(e, 5, ESNULLP);
        CHECK(r == 0);
        CHECK(strcmp(e, "n...") == 0);
        CHECK(strlen(e) == 4);
    }
    {
        char e[16];
        errno_t r = cplus_strerror_s(e, 6, ESNULLP);
        CHECK(r == 0);
        CHECK(strcmp(e, "nu...") == 0);
    }
    {
        char e[16];
        errno_t r = cplus_strerror_s(e, 4, ESNULLP); /* dmax-4 == 0 chars */
        CHECK(r == 0);
        CHECK(strcmp(e, "...") == 0);
        CHECK(strlen(e) == 3);
    }
    /* a longer message with the same marker rule */
    {
        char e[32];
        errno_t r = cplus_strerror_s(e, 14, ESZEROL); /* "length is zero" is 14 */
        CHECK(r == 0);
        CHECK(strstr(e, "...") != NULL);
        CHECK(strlen(e) == 13); /* 10 chars + "..." */
        CHECK(strncmp(e, "length is ", 10) == 0);
    }
    /* tiny buffers below the marker size -> ESLEMIN, dest untouched */
    {
        char e[8];
        memset(e, 0x5A, sizeof e);
        CHECK(cplus_strerror_s(e, 3, ESNULLP) == ESLEMIN);
        CHECK((unsigned char)e[0] == 0x5A);
        CHECK((unsigned char)e[7] == 0x5A);
    }
    {
        char e[8];
        memset(e, 0x5A, sizeof e);
        CHECK(cplus_strerror_s(e, 2, ESNULLP) == ESLEMIN);
    }
    {
        char e[8];
        memset(e, 0x5A, sizeof e);
        CHECK(cplus_strerror_s(e, 1, ESNULLP) == ESLEMIN);
    }
    /* dmax == 0 and NULL dest */
    {
        char e[8];
        CHECK(cplus_strerror_s(e, 0, ESNULLP) == ESZEROL);
        CHECK(cplus_strerror_s(NULL, 8, ESNULLP) == ESNULLP);
    }
    /* guard bytes past dmax survive truncation */
    {
        char g[16];
        errno_t r;
        memset(g, 0x5A, sizeof g);
        r = cplus_strerror_s(g, 8, ESNULLP);
        CHECK(r == 0);
        CHECK((unsigned char)g[8] == 0x5A);
        CHECK((unsigned char)g[15] == 0x5A);
    }
    /* 100-iteration loops: repeated normal and truncating calls stay stable */
    {
        int i;
        int normal = 0;
        int trunc = 0;
        for (i = 0; i < 100; i++) {
            char e[64];
            if (cplus_strerror_s(e, 64, ESNULLP) == 0 &&
                strcmp(e, "null ptr") == 0)
                normal++;
        }
        CHECK(normal == 100);

        for (i = 0; i < 100; i++) {
            char e[8];
            if (cplus_strerror_s(e, 8, ESNULLP) == 0 &&
                strstr(e, "...") != NULL && strlen(e) < 8)
                trunc++;
        }
        CHECK(trunc == 100);
    }

    if (failures == 0) {
        printf("test_str50_strerror: ok\n");
        return 0;
    }
    printf("test_str50_strerror: FAILURES=%d\n", failures);
    return 1;
}
