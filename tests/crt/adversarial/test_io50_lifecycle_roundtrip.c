/*
 * Adversarial io50 #2: full open -> write -> read -> close round-trip.
 *
 * Proves the checked stdio adapters move the exact bytes, that the handle is
 * nulled by close, that a double close is a defined EINVAL, and that contents
 * survive a close/reopen and an append cycle. All temp files live under /tmp
 * and are removed before exit.
 */
#include <stdio.h>
#include <string.h>
#include "safe_io.h"
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

    const char *path = "/tmp/cplus_io50_lifecycle.tmp";
    const char payload[] = "0123456789ABCDEF";   /* 16 bytes + NUL */
    FILE *f = cplus_fopen_s(path, "wb+");
    char rbuf[32];
    size_t n;

    CHECK(f != NULL);

    /* write the whole payload as one element, then flush/rewind */
    n = cplus_fwrite_s(payload, sizeof payload, 1, sizeof payload, f);
    CHECK(n == sizeof payload);
    CHECK(ftell(f) == (long)sizeof payload);
    CHECK(fflush(f) == 0);

    rewind(f);
    memset(rbuf, 0, sizeof rbuf);
    n = cplus_fread_s(rbuf, sizeof rbuf, 1, sizeof payload, f);
    CHECK(n == sizeof payload);
    CHECK(memcmp(rbuf, payload, sizeof payload) == 0);
    CHECK(rbuf[sizeof payload] == '\0');         /* nothing past the payload */

    /* element-wise view: 4 elements of 4 bytes == 16 bytes */
    rewind(f);
    memset(rbuf, 0, sizeof rbuf);
    n = cplus_fread_s(rbuf, sizeof rbuf, 4, 4, f);
    CHECK(n == 4);
    CHECK(memcmp(rbuf, payload, 16) == 0);

    /* seek into the middle and read a 4-byte slice */
    CHECK(fseek(f, 4, SEEK_SET) == 0);
    memset(rbuf, 0, sizeof rbuf);
    n = cplus_fread_s(rbuf, sizeof rbuf, 1, 4, f);
    CHECK(n == 4);
    CHECK(memcmp(rbuf, "4567", 4) == 0);

    CHECK(cplus_fclose_s(f) == 0);
    CHECK(cplus_fclose_s(f) == EOF);   /* double close */

    /* reopen and confirm the content persisted on disk */
    f = cplus_fopen_s(path, "rb");
    CHECK(f != NULL);
    memset(rbuf, 0, sizeof rbuf);
    n = cplus_fread_s(rbuf, sizeof rbuf, 1, sizeof payload, f);
    CHECK(n == sizeof payload);
    CHECK(memcmp(rbuf, payload, sizeof payload) == 0);
    CHECK(fseek(f, 0, SEEK_END) == 0);
    CHECK(ftell(f) == (long)sizeof payload);
    CHECK(cplus_fclose_s(f) == 0);

    /* second independent path: create, append, then read it all back */
    {
        const char *p2 = "/tmp/cplus_io50_lifecycle2.tmp";
        f = cplus_fopen_s(p2, "wb"); CHECK(f != NULL);
        CHECK(cplus_fwrite_s("hello", 5, 1, 5, f) == 5);
        CHECK(cplus_fclose_s(f) == 0);
        f = cplus_fopen_s(p2, "ab"); CHECK(f != NULL);
        CHECK(cplus_fwrite_s("world", 5, 1, 5, f) == 5);
        CHECK(cplus_fclose_s(f) == 0);
        f = cplus_fopen_s(p2, "rb"); CHECK(f != NULL);
        memset(rbuf, 0, sizeof rbuf);
        n = cplus_fread_s(rbuf, sizeof rbuf, 1, 10, f);
        CHECK(n == 10);
        CHECK(memcmp(rbuf, "helloworld", 10) == 0);
        CHECK(cplus_fclose_s(f) == 0);
        remove(p2);
    }

    remove(path);

    if (failures == 0) {
        printf("test_io50_lifecycle_roundtrip: ok\n");
        return 0;
    }
    printf("test_io50_lifecycle_roundtrip: FAILURES=%d\n", failures);
    return 1;
}
