/*
 * Bug CLASS: format-string / sprintf buffer overflow.
 *
 * Raw C: sprintf(buf, "%s", huge) writes past buf. The checked C+ bounded
 * formatters (cplus_snprintf_s / cplus_sprintf_s) never write past dmax, always
 * NUL-terminate (when dmax > 0), and return the number of bytes that WOULD have
 * been written so the caller can size a retry.
 */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "safe_format.h"
#include "safe_error.h"
#include "safe_log.h"

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

    /* truncated %s: returns full required length, output bounded + terminated */
    char buf[6];
    memset(buf, 'Z', sizeof buf);
    int r = cplus_snprintf_s(buf, sizeof buf, "%s", "abcdefghij");
    CHECK(r == 10);
    CHECK((int)strlen(buf) == 5);
    CHECK(buf[5] == '\0');
    CHECK(memcmp(buf, "abcde", 5) == 0);

    /* guard bytes beyond dmax must never be written */
    char big[16];
    memset(big, 0x5A, sizeof big);
    r = cplus_snprintf_s(big, 6, "%d", 123456789);
    CHECK(r == 9);
    CHECK((int)strlen(big) == 5);
    CHECK(big[5] == '\0');
    for (int i = 6; i < 16; ++i) CHECK(big[i] == 0x5A);

    /* sprintf_s is the same bounded behavior (NOT Annex K reject-on-overflow) */
    memset(big, 0x5A, sizeof big);
    r = cplus_sprintf_s(big, 6, "%s", "abcdefghij");
    CHECK(r == 10);
    CHECK((int)strlen(big) == 5);
    for (int i = 6; i < 16; ++i) CHECK(big[i] == 0x5A);

    /* dmax == 1: only the terminator fits */
    char one[4];
    memset(one, 'Z', sizeof one);
    CHECK(cplus_snprintf_s(one, 1, "%s", "xyz") == 3);
    CHECK(one[0] == '\0');

    /* dmax == 0: writes nothing, reports the required length */
    char zero[4];
    memset(zero, 'Z', sizeof zero);
    CHECK(cplus_snprintf_s(zero, 0, "%s", "xyz") == 3);
    CHECK(zero[0] == 'Z');

    /* exact fit succeeds and NUL-terminates */
    char exact[8];
    CHECK(cplus_snprintf_s(exact, sizeof exact, "%s", "hello") == 5);
    CHECK(strcmp(exact, "hello") == 0);
    CHECK(cplus_snprintf_s(exact, sizeof exact, "%d", 123456789) == 9);

    /* invalid arguments are defined errors, not crashes */
    errno = 0;
    CHECK(cplus_snprintf_s(buf, sizeof buf, NULL) == -1);
    CHECK(errno == EINVAL);
    CHECK(cplus_snprintf_s(NULL, sizeof buf, "%s", "x") == -1);
    /* the size-query form is well defined */
    CHECK(cplus_snprintf_s(NULL, 0, "%d", 7) == 1);

    if (failures == 0) {
        printf("bugclass_format: ok\n");
        return 0;
    }
    printf("bugclass_format: FAILURES=%d\n", failures);
    return 1;
}
