#include <errno.h>
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

    const char *path = "/tmp/cplus_io50_open_contract.tmp";
    const char payload[] = "io50-open-contract";
    char rbuf[64];
    size_t got;

    errno = 0; CHECK(cplus_fopen_s(NULL, "wb") == NULL && errno == EINVAL);
    errno = 0; CHECK(cplus_fopen_s(path, NULL) == NULL && errno == EINVAL);
    errno = 0; CHECK(cplus_fopen_s(NULL, NULL) == NULL && errno == EINVAL);

    CHECK(cplus_fopen_s("/no/such/cplus/io50/dir/file.tmp", "rb") == NULL);
    CHECK(cplus_fopen_s("", "wb") == NULL);
    CHECK(cplus_fopen_s(path, "") == NULL);
    CHECK(cplus_fopen_s("/", "wb") == NULL);

    errno = 0; CHECK(cplus_fclose_s(NULL) == EOF && errno == EINVAL);

    FILE *f = cplus_fopen_s(path, "wb+");
    CHECK(f != NULL);
    got = cplus_fwrite_s(payload, sizeof payload, 1, sizeof payload, f);
    CHECK(got == sizeof payload);
    rewind(f);
    memset(rbuf, 0, sizeof rbuf);
    got = cplus_fread_s(rbuf, sizeof rbuf, 1, sizeof payload, f);
    CHECK(got == sizeof payload && memcmp(rbuf, payload, sizeof payload) == 0);
    CHECK(cplus_fclose_s(f) == 0);
    errno = 0; CHECK(cplus_fclose_s(f) == EOF && errno == EINVAL);

    f = cplus_fopen_s(path, "rb");
    CHECK(f != NULL && cplus_fclose_s(f) == 0);

    remove(path);

    if (failures == 0) { printf("test_io50_open_contract: ok\n"); return 0; }
    printf("test_io50_open_contract: FAILURES=%d\n", failures);
    return 1;
}
