/*
 * Adversarial io50 #3: fread_s / fwrite_s argument hardening.
 *
 * Raw-C traps: fread/fwrite into a NULL buffer, with a NULL stream, or with a
 * zero size/count; and a bound of 0 (or smaller than one element) that must
 * never let an unbounded count overflow the caller's buffer. Every degenerate
 * call is a defined 0 with no I/O performed and the buffer left untouched.
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

    const char *path = "/tmp/cplus_io50_nullargs.tmp";
    const char payload[] = "abcdefgh";
    FILE *f = cplus_fopen_s(path, "wb+");
    char buf[32];

    CHECK(f != NULL);

    memset(buf, 0x5A, sizeof buf);

    /* --- fwrite_s: any NULL/zero argument yields a defined 0, no I/O --- */
    CHECK(cplus_fwrite_s(NULL, sizeof buf, 1, 1, f) == 0);    /* NULL buffer */
    CHECK(cplus_fwrite_s(buf, sizeof buf, 1, 1, NULL) == 0);  /* NULL stream */
    CHECK(cplus_fwrite_s(buf, sizeof buf, 0, 1, f) == 0);     /* size == 0   */
    CHECK(cplus_fwrite_s(buf, sizeof buf, 1, 0, f) == 0);     /* count == 0  */
    CHECK(cplus_fwrite_s(NULL, sizeof buf, 0, 0, NULL) == 0); /* all bad     */
    CHECK(cplus_fwrite_s(NULL, 0, 0, 0, NULL) == 0);          /* all bad     */

    /* --- fread_s: the same contract --- */
    CHECK(cplus_fread_s(NULL, sizeof buf, 1, 1, f) == 0);     /* NULL buffer */
    CHECK(cplus_fread_s(buf, sizeof buf, 1, 1, NULL) == 0);   /* NULL stream */
    CHECK(cplus_fread_s(buf, sizeof buf, 0, 1, f) == 0);      /* size == 0   */
    CHECK(cplus_fread_s(buf, sizeof buf, 1, 0, f) == 0);      /* count == 0  */
    CHECK(cplus_fread_s(NULL, sizeof buf, 0, 0, NULL) == 0);  /* all bad     */
    CHECK(cplus_fread_s(NULL, 0, 0, 0, NULL) == 0);           /* all bad     */

    /* --- none of the rejected calls moved the stream or wrote anything --- */
    CHECK(ftell(f) == 0);

    /* --- bound 0 (or smaller than one element) clamps the count to 0 --- */
    CHECK(cplus_fwrite_s(buf, 0, 1, 4, f) == 0);              /* bmax == 0   */
    CHECK(cplus_fread_s(buf, 0, 1, 4, f) == 0);
    CHECK(cplus_fwrite_s(buf, 1, 4, 2, f) == 0);              /* 1 < 4       */
    CHECK(cplus_fread_s(buf, 1, 4, 2, f) == 0);

    /* --- the buffer was never touched by any rejected read --- */
    CHECK(buf[0] == (char)0x5A);
    CHECK(buf[sizeof buf - 1] == (char)0x5A);
    CHECK(ftell(f) == 0);

    /* --- sanity: the same stream still performs a valid round-trip --- */
    CHECK(cplus_fwrite_s(payload, sizeof payload, 1, sizeof payload, f) == sizeof payload);
    CHECK(ftell(f) == (long)sizeof payload);
    rewind(f);
    memset(buf, 0, sizeof buf);
    CHECK(cplus_fread_s(buf, sizeof buf, 1, sizeof payload, f) == sizeof payload);
    CHECK(memcmp(buf, payload, sizeof payload) == 0);

    CHECK(cplus_fclose_s(f) == 0);
    CHECK(cplus_fclose_s(f) == EOF);          /* double close */

    remove(path);

    if (failures == 0) {
        printf("test_io50_null_args: ok\n");
        return 0;
    }
    printf("test_io50_null_args: FAILURES=%d\n", failures);
    return 1;
}
