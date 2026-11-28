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

#define ITERS 100

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);

    const char *path = "/tmp/cplus_io50_loop.tmp";
    const char payload[] = "0123456789abcdef";
    char rbuf[32];
    FILE *f = NULL;
    int i;
    int ok = 0, bad = 0, dbl = 0, rej = 0, failnull = 0;

    for (i = 0; i < ITERS; ++i) {
        size_t n;
        f = cplus_fopen_s(path, "wb+");
        if (!f) { bad++; continue; }
        if (cplus_fwrite_s(payload, sizeof payload, 1, sizeof payload, f) != sizeof payload) { (void)cplus_fclose_s(f); bad++; continue; }
        rewind(f);
        memset(rbuf, 0, sizeof rbuf);
        n = cplus_fread_s(rbuf, sizeof rbuf, 1, sizeof payload, f);
        if (n != sizeof payload || memcmp(rbuf, payload, sizeof payload) != 0) { (void)cplus_fclose_s(f); bad++; continue; }
        if (cplus_fclose_s(f) != 0) { bad++; continue; }
        ok++;
    }
    CHECK(ok == ITERS && bad == 0);

    f = cplus_fopen_s(path, "rb"); CHECK(f != NULL);
    memset(rbuf, 0, sizeof rbuf);
    CHECK(cplus_fread_s(rbuf, sizeof rbuf, 1, sizeof payload, f) == sizeof payload);
    CHECK(memcmp(rbuf, payload, sizeof payload) == 0);
    CHECK(cplus_fclose_s(f) == 0);

    for (i = 0; i < ITERS; ++i) {
        f = cplus_fopen_s("/no/such/cplus/io50/path.tmp", "rb");
        if (f != NULL) failnull++;
    }
    CHECK(failnull == 0);

    for (i = 0; i < ITERS; ++i) {
        f = cplus_fopen_s(path, "rb");
        if (!f) continue;
        if (cplus_fclose_s(f) != 0) continue;
        if (cplus_fclose_s(f) == EOF) dbl++;
    }
    CHECK(dbl == ITERS);

    f = cplus_fopen_s(path, "rb+"); CHECK(f != NULL);
    for (i = 0; i < ITERS; ++i) {
        if (cplus_fwrite_s(NULL, sizeof rbuf, 1, 1, f) == 0 &&
            cplus_fread_s(NULL, sizeof rbuf, 1, 1, f) == 0 &&
            cplus_fwrite_s(rbuf, sizeof rbuf, 1, 1, NULL) == 0)
            rej++;
    }
    CHECK(rej == ITERS && ftell(f) == 0);
    CHECK(cplus_fclose_s(f) == 0);

    f = cplus_fopen_s(path, "wb+"); CHECK(f != NULL);
    for (i = 0; i < ITERS; ++i) {
        rewind(f);
        if (cplus_fwrite_s(payload, sizeof payload, 1, 64, f) != sizeof payload) bad++;
    }
    CHECK(bad == 0 && ftell(f) == (long)sizeof payload);
    CHECK(cplus_fclose_s(f) == 0);

    CHECK(cplus_fopen_s(path, NULL) == NULL);
    CHECK(cplus_fopen_s(NULL, "wb") == NULL);
    CHECK(cplus_fclose_s(NULL) == EOF);
    CHECK(cplus_fread_s(NULL, 4, 1, 1, NULL) == 0);
    CHECK(cplus_fwrite_s(NULL, 4, 1, 1, NULL) == 0);

    remove(path);

    if (failures == 0) { printf("test_io50_loop100: ok\n"); return 0; }
    printf("test_io50_loop100: FAILURES=%d\n", failures);
    return 1;
}
