/*
 * Adversarial io50 #4: fread_s / fwrite_s count clamping to bmax.
 *
 * The adapters clamp `count` to `bmax / size` so a truthful bound can never be
 * overrun, even when the caller asks for far more elements than the buffer
 * holds. This test pins the exact clamped element counts and shows that the
 * bytes past the bound are never written.
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

    const char *path = "/tmp/cplus_io50_clamp.tmp";
    const char payload[] = "ABCDEFGHIJKLMNOP";   /* 16 bytes */
    FILE *f = cplus_fopen_s(path, "wb+");
    char r[32];
    size_t n;

    CHECK(f != NULL);

    /* --- writes: exact clamped element count --- */
    CHECK(cplus_fwrite_s(payload, 4, 1, 8, f) == 4);   /* 4/1 = 4 elements -> 4 */
    CHECK(ftell(f) == 4);
    CHECK(cplus_fwrite_s(payload, 8, 2, 100, f) == 4); /* 8/2 = 4 elements -> 8 */
    CHECK(ftell(f) == 12);
    CHECK(cplus_fwrite_s(payload, 5, 2, 9, f) == 2);   /* 5/2 = 2 elements -> 4 */
    CHECK(ftell(f) == 16);
    CHECK(cplus_fwrite_s(payload, 0, 1, 3, f) == 0);   /* 0/1 = 0 elements -> 0 */
    CHECK(ftell(f) == 16);
    CHECK(cplus_fwrite_s(payload, 3, 3, 10, f) == 1);  /* 3/3 = 1 element  -> 3 */
    CHECK(ftell(f) == 19);
    CHECK(cplus_fwrite_s(payload, 16, 1, 16, f) == 16);/* exact fit, no clamp  */
    CHECK(ftell(f) == 35);

    /* --- reads obey the same clamp; guard bytes past the bound survive --- */
    rewind(f);
    memset(r, 0x7E, sizeof r);
    n = cplus_fread_s(r, 4, 1, 8, f);                  /* clamp to 4 */
    CHECK(n == 4);
    CHECK(memcmp(r, "ABCD", 4) == 0);
    CHECK(r[4] == (char)0x7E);                          /* nothing past bound */

    rewind(f);
    memset(r, 0x7E, sizeof r);
    n = cplus_fread_s(r, 8, 2, 100, f);                /* clamp to 4 elements */
    CHECK(n == 4);
    CHECK(r[8] == (char)0x7E);

    rewind(f);
    {
        char s[8];
        memset(s, 0x7E, sizeof s);
        CHECK(cplus_fread_s(s, 5, 2, 9, f) == 2);      /* clamp to 2 elements */
        CHECK(s[4] == (char)0x7E);
    }

    rewind(f);
    memset(r, 0x7E, sizeof r);
    CHECK(cplus_fread_s(r, 0, 1, 3, f) == 0);          /* clamp to 0 */
    CHECK(r[0] == (char)0x7E);

    /* --- a huge count must not overflow the clamp arithmetic --- */
    rewind(f);
    memset(r, 0x7E, sizeof r);
    n = cplus_fread_s(r, 16, 8, (size_t)-1, f);        /* clamp to 16/8 = 2  */
    CHECK(n == 2);
    CHECK(r[16] == (char)0x7E);

    CHECK(cplus_fclose_s(f) == 0);

    remove(path);

    if (failures == 0) {
        printf("test_io50_clamp_bmax: ok\n");
        return 0;
    }
    printf("test_io50_clamp_bmax: FAILURES=%d\n", failures);
    return 1;
}
