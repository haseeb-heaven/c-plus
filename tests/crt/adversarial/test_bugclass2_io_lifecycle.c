/*
 * Bug CLASS: stdio lifecycle misuse.
 *
 * Raw C traps: fopen(NULL), fclose twice, fread/fwrite into an undersized or
 * NULL buffer, reading more elements than the buffer can hold. The checked C+
 * stdio adapters turn each into a defined result:
 *   - fopen_s validates out/name/mode; a failed open returns non-zero and
 *     leaves the handle NULL.
 *   - fclose_s clears the handle; a second close is a defined error, not UB.
 *   - fread_s/fwrite_s return 0 on NULL/degenerate args and CLAMP `count` to
 *     bmax/size so they can never write past the caller's truthful bound.
 *   - a write/rewind/read round-trip returns the exact bytes.
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

    const char *path = "/tmp/cplus_h2_io_lifecycle.tmp";
    const char payload[] = "ABCDEFGH";   /* 8 bytes, no NUL sentinel needed */
    FILE *f = NULL;
    char buf[16];

    /* --- NULL argument contract: never dereferenced --- */
    CHECK(cplus_fopen_s(NULL, path, "wb") == CPLUS_SAFE_EINVAL);
    CHECK(cplus_fopen_s(&f, NULL, "wb") == CPLUS_SAFE_EINVAL);
    CHECK(cplus_fopen_s(&f, path, NULL) == CPLUS_SAFE_EINVAL);
    CHECK(f == NULL);

    /* --- failed open: handle stays NULL, error is reported --- */
    CHECK(cplus_fopen_s(&f, "/no/such/cplus/h2/lifecycle", "rb") != CPLUS_SAFE_OK);
    CHECK(f == NULL);

    /* --- fclose of a NULL out-param or a NULL handle is a defined error --- */
    CHECK(cplus_fclose_s(NULL) == CPLUS_SAFE_EINVAL);
    CHECK(cplus_fclose_s(&f) == CPLUS_SAFE_EINVAL);   /* f == NULL */

    /* --- open a scratch stream for the round-trip and clamping checks --- */
    CHECK(cplus_fopen_s(&f, path, "wb+") == CPLUS_SAFE_OK);
    CHECK(f != NULL);

    /* --- read/write with NULL buf, NULL stream, size 0, count 0 -> 0, no I/O - */
    memset(buf, 0, sizeof buf);
    CHECK(cplus_fwrite_s(NULL, sizeof buf, 1, 1, f) == 0);    /* NULL buffer */
    CHECK(cplus_fwrite_s(buf, sizeof buf, 1, 1, NULL) == 0);  /* NULL stream */
    CHECK(cplus_fwrite_s(buf, sizeof buf, 0, 1, f) == 0);     /* size == 0   */
    CHECK(cplus_fwrite_s(buf, sizeof buf, 1, 0, f) == 0);     /* count == 0  */
    CHECK(cplus_fread_s(NULL, sizeof buf, 1, 1, f) == 0);     /* NULL buffer */
    CHECK(cplus_fread_s(buf, sizeof buf, 1, 1, NULL) == 0);   /* NULL stream */
    CHECK(cplus_fread_s(buf, sizeof buf, 0, 1, f) == 0);      /* size == 0   */
    CHECK(cplus_fread_s(buf, sizeof buf, 1, 0, f) == 0);      /* count == 0  */

    /* --- write with count clamped to bmax/size: ask 8, bound 4 -> writes 4 --- */
    CHECK(cplus_fwrite_s(payload, 4, 1, 8, f) == 4);

    /* --- read it back with the same clamp; exact bytes returned --- */
    rewind(f);
    {
        char r[16];
        size_t n;
        memset(r, 0, sizeof r);
        n = cplus_fread_s(r, 4, 1, 8, f);         /* clamped to 4 elements */
        CHECK(n == 4);
        CHECK(memcmp(r, "ABCD", 4) == 0);
        CHECK(r[4] == '\0');                      /* nothing past the bound */
    }

    /* --- a truthful, exactly-sized read also returns the exact bytes --- */
    rewind(f);
    {
        char r[16];
        memset(r, 0, sizeof r);
        CHECK(cplus_fread_s(r, sizeof r, 1, 4, f) == 4);
        CHECK(memcmp(r, "ABCD", 4) == 0);
    }

    /* --- bound smaller than one element -> clamped count 0 -> returns 0 --- */
    rewind(f);
    CHECK(cplus_fread_s(buf, 1, 4, 2, f) == 0);

    /* --- close exactly once: handle cleared, double close defined --- */
    CHECK(cplus_fclose_s(&f) == CPLUS_SAFE_OK);
    CHECK(f == NULL);
    CHECK(cplus_fclose_s(&f) == CPLUS_SAFE_EINVAL);   /* double close caught */

    remove(path);

    if (failures == 0) {
        printf("bugclass2_io_lifecycle: ok\n");
        return 0;
    }
    printf("bugclass2_io_lifecycle: FAILURES=%d\n", failures);
    return 1;
}
