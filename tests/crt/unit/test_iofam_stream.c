/*
 * Unit: checked `cplus_fread_s` / `cplus_fwrite_s` stream buffer contract.
 *
 * Maps to catalogue bug110/bug111/bug112 (stream buffer lifecycle: reads and
 * writes are bounded by the caller-declared buffer max `bmax`, no read/write is
 * attempted with a NULL buffer or stream, and a zero element size/count is a
 * defined no-op). bug82 is the underlying "fread does not NUL-terminate" trap,
 * so every read-back is length-checked here.
 *
 * Contract asserted here:
 *   - NULL buf / NULL stream / size==0 / count==0 -> returns 0, no I/O
 *   - count is clamped to bmax / size (never overruns the caller buffer)
 *   - a write followed by rewind + read round-trips the exact bytes
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "safe_io.h"
#include "safe_error.h"
#include "safe_log.h"

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);

    const char *path = "/tmp/cplus_iofam_stream.tmp";
    const char payload[] = "ABCDEFGH";      /* 8 bytes, no terminator sentinel */
    FILE *f = NULL;
    char buf[16];

    /* ---- 1. open the buffer under test ---- */
    assert(cplus_fopen_s(&f, path, "wb+") == CPLUS_SAFE_OK);

    /* ---- 2-5. fwrite_s invalid arguments all return 0 and touch nothing ---- */
    assert(cplus_fwrite_s(NULL, sizeof payload, 1, 1, f) == 0); /* NULL buf */
    assert(cplus_fwrite_s(payload, sizeof payload, 1, 1, NULL) == 0); /* NULL stream */
    assert(cplus_fwrite_s(payload, sizeof payload, 0, 1, f) == 0);    /* size == 0 */
    assert(cplus_fwrite_s(payload, sizeof payload, 1, 0, f) == 0);    /* count == 0 */

    /* ---- 6. count is clamped to bmax/size: ask 8 items, bound 4 -> writes 4 ---- */
    assert(cplus_fwrite_s(payload, 4, 1, 8, f) == 4);

    /* ---- 7-10. fread_s invalid arguments all return 0 and touch nothing ---- */
    assert(cplus_fread_s(NULL, sizeof buf, 1, 1, f) == 0);            /* NULL buf */
    assert(cplus_fread_s(buf, sizeof buf, 1, 1, NULL) == 0);          /* NULL stream */
    assert(cplus_fread_s(buf, sizeof buf, 0, 1, f) == 0);             /* size == 0 */
    assert(cplus_fread_s(buf, sizeof buf, 1, 0, f) == 0);             /* count == 0 */

    /* ---- 11-13. round-trip the clamped write through a clamped read ---- */
    rewind(f);
    {
        size_t n = cplus_fread_s(buf, 4, 1, 8, f);   /* clamped to 4 items */
        assert(n == 4);
        assert(memcmp(buf, "ABCD", 4) == 0);
        assert(buf[3] == 'D');
    }

    /* ---- 14-15. exact-size read of a raw byte window (bug82: length-checked) ---- */
    rewind(f);
    {
        unsigned char raw[8] = { 0 };
        assert(cplus_fread_s(raw, sizeof raw, 1, 4, f) == 4);
        assert(raw[0] == 'A' && raw[2] == 'C' && raw[3] == 'D');
    }

    /* ---- 16. bmax < one element -> clamped count is 0 -> no I/O, returns 0 ---- */
    assert(cplus_fread_s(buf, 1, 4, 2, f) == 0);

    /* ---- 17-18. close exactly once; owned handle cleared (buffer released) ---- */
    assert(cplus_fclose_s(&f) == CPLUS_SAFE_OK);
    assert(f == NULL);

    remove(path);
    puts("test_iofam_stream: ok");
    return 0;
}
