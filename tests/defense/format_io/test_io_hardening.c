/* BLUE-TEAM agent 9: defense-in-depth regressions for the checked I/O API.
 *
 * Host fread()/fwrite() on a FILE* that cplus_fclose_s() already closed is a
 * use-after-free; previously cplus_fread_s/cplus_fwrite_s only checked for NULL
 * and could reach the host with a freed stream.  This test pins that the
 * wrappers REFUSE (return 0, EINVAL) before any host I/O, that a truthful bmax
 * on a tracked allocation is enforced independently of the caller's clamp, and
 * that valid round-trips / clamps are unchanged.
 */
#define _POSIX_C_SOURCE 200112L
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "safe_alloc.h"
#include "safe_error.h"
#include "safe_io.h"
#include "safe_log.h"

static int failures = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            ++failures;                                                        \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__,   \
                    #cond);                                                    \
        }                                                                      \
    } while (0)

/* ---------------------------------------------------------------------------
 * 1. NEW GAP: fread_s/fwrite_s on a stream closed through cplus_fclose_s.
 * ------------------------------------------------------------------------- */
static void test_closed_stream(void) {
    const char *path = "/tmp/cplus_def_io_closed.tmp";
    const char payload[] = "ABCDEFGHIJKLMNOP";
    FILE *f = cplus_fopen_s(path, "wb+");
    char r[16];
    size_t n;

    CHECK(f != NULL);
    CHECK(cplus_fwrite_s(payload, sizeof payload - 1, 1, sizeof payload - 1, f) ==
          sizeof payload - 1);
    CHECK(cplus_fclose_s(f) == 0);

    memset(r, 'Z', sizeof r);
    errno = 0;
    n = cplus_fread_s(r, sizeof r, 1, 4, f); /* was a UAF on host fread */
    CHECK(n == 0);
    CHECK(errno == EINVAL);
    CHECK(r[0] == 'Z'); /* nothing touched */

    errno = 0;
    n = cplus_fwrite_s(payload, sizeof payload - 1, 1, 4, f); /* UAF on fwrite */
    CHECK(n == 0);
    CHECK(errno == EINVAL);

    /* Double close is still refused (defense in depth stays consistent). */
    errno = 0;
    CHECK(cplus_fclose_s(f) == EOF);
    CHECK(errno == EINVAL);
    unlink(path);
}

/* ---------------------------------------------------------------------------
 * 2. NEW GAP: bmax larger than the real tracked allocation must be REFUSED
 *    even when the caller's declared count fits inside the inflated bmax.
 *    Previously count <= bmax/size was the only bound, so a lying bmax could
 *    let fread write past the real buffer.
 * ------------------------------------------------------------------------- */
static void test_tracked_bmax(void) {
    const char *path = "/tmp/cplus_def_io_bmax.tmp";
    FILE *f = cplus_fopen_s(path, "wb+");
    char *hp;
    char guard[16];
    size_t n;

    CHECK(f != NULL);
    CHECK(cplus_fwrite_s("0123456789ABCDEF", 16, 1, 16, f) == 16);
    rewind(f);

    hp = (char *)malloc_s(8);
    CHECK(hp != NULL);
    memset(hp, 'Z', 8);
    /* Declare bmax=16 (2x the real allocation) and ask for 16 one-byte items.
     * Old code clamped to 16/1=16 and read 16 bytes into an 8-byte buffer. */
    errno = 0;
    n = cplus_fread_s(hp, 16, 1, 16, f);
    CHECK(n == 0); /* REFUSED */
    CHECK(errno == EINVAL);
    CHECK(hp[0] == 'Z' && hp[7] == 'Z'); /* untouched */

    /* A truthful bmax still works: exactly 8 bytes, no overflow. */
    rewind(f);
    n = cplus_fread_s(hp, 8, 1, 8, f);
    CHECK(n == 8);
    CHECK(memcmp(hp, "01234567", 8) == 0);
    free_s(hp);

    /* Freed buffer is refused via the same independent check. */
    hp = (char *)malloc_s(8);
    CHECK(hp != NULL);
    free_s(hp);
    rewind(f);
    errno = 0;
    n = cplus_fread_s(hp, 8, 1, 8, f);
    CHECK(n == 0); /* REFUSED, use-after-free */
    CHECK(errno == EINVAL);

    /* Stack buffers are untracked; the caller clamp still protects them. */
    rewind(f);
    memset(guard, 'Z', sizeof guard);
    n = cplus_fread_s(guard, 4, 1, sizeof guard, f);
    CHECK(n == 4);
    CHECK(guard[4] == 'Z'); /* nothing past the declared bound */

    CHECK(cplus_fclose_s(f) == 0);
    unlink(path);
}

/* ---------------------------------------------------------------------------
 * 3. Huge n/count and bmax clamping (regression of already-fixed behavior).
 * ------------------------------------------------------------------------- */
static void test_huge_counts(void) {
    const char *path = "/tmp/cplus_def_io_huge.tmp";
    FILE *f = cplus_fopen_s(path, "wb+");
    char buf[32];
    size_t n;

    CHECK(f != NULL);
    CHECK(cplus_fwrite_s("ABCDEFGHIJKLMNOP", 16, 1, 16, f) == 16);
    rewind(f);
    memset(buf, 'Z', sizeof buf);
    n = cplus_fread_s(buf, 16, 8, (size_t)-1, f); /* clamp to 16/8 = 2 */
    CHECK(n == 2);
    CHECK(buf[16] == 'Z'); /* no write past the declared bmax */
    rewind(f);
    memset(buf, 'Z', sizeof buf);
    n = cplus_fread_s(buf, 0, 1, (size_t)-1, f);
    CHECK(n == 0);
    CHECK(buf[0] == 'Z');
    CHECK(cplus_fclose_s(f) == 0);
    unlink(path);
}

/* ---------------------------------------------------------------------------
 * 4. Null args and zero size/count still refuse (defined behavior).
 * ------------------------------------------------------------------------- */
static void test_null_args(void) {
    const char *path = "/tmp/cplus_def_io_null.tmp";
    FILE *f = cplus_fopen_s(path, "wb+");
    char buf[8];
    CHECK(f != NULL);
    CHECK(cplus_fread_s(NULL, sizeof buf, 1, 1, f) == 0);
    CHECK(cplus_fread_s(buf, sizeof buf, 1, 1, NULL) == 0);
    CHECK(cplus_fwrite_s(NULL, sizeof buf, 1, 1, f) == 0);
    CHECK(cplus_fwrite_s(buf, sizeof buf, 1, 1, NULL) == 0);
    CHECK(cplus_fread_s(buf, sizeof buf, 0, 1, f) == 0);
    CHECK(cplus_fwrite_s(buf, sizeof buf, 0, 1, f) == 0);
    CHECK(cplus_fread_s(buf, sizeof buf, 1, 0, f) == 0);
    CHECK(cplus_fwrite_s(buf, sizeof buf, 1, 0, f) == 0);
    CHECK(cplus_fclose_s(f) == 0);
    unlink(path);
}

/* ---------------------------------------------------------------------------
 * 5. Valid round-trip and fopen/fclose contract unchanged.
 * ------------------------------------------------------------------------- */
static void test_valid(void) {
    const char *path = "/tmp/cplus_def_io_valid.tmp";
    const char src[] = "hello world";
    char dst[32] = {0};
    FILE *f = cplus_fopen_s(path, "wb+");
    CHECK(f != NULL);
    CHECK(cplus_fwrite_s(src, sizeof src - 1, 1, sizeof src - 1, f) ==
          sizeof src - 1);
    rewind(f);
    CHECK(cplus_fread_s(dst, sizeof dst, 1, sizeof src - 1, f) == sizeof src - 1);
    CHECK(strcmp(dst, src) == 0);
    CHECK(cplus_fclose_s(f) == 0);
    unlink(path);

    errno = 0;
    CHECK(cplus_fopen_s(NULL, "r") == NULL && errno == EINVAL);
    errno = 0;
    CHECK(cplus_fclose_s(NULL) == EOF && errno == EINVAL);
}

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);

    test_closed_stream();
    test_tracked_bmax();
    test_huge_counts();
    test_null_args();
    test_valid();

    if (failures == 0) {
        puts("test_io_hardening: ok");
        return 0;
    }
    printf("test_io_hardening: FAILURES=%d\n", failures);
    return 1;
}
