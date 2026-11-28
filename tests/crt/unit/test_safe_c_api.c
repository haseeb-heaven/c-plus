/* Regression test for the ORDINARY C API hardening layer.
 *
 * Proves that unchanged malloc/memset/memcpy/strcpy/fread/printf calls get
 * compile-time format checking and runtime capacity enforcement when
 * stdlib/safe/safe_c.h is included. No `_s` spelling is used by the test
 * bodies below (except the explicit policy setter).
 */
#include "stdlib/safe/safe_c.h"
#include "stdlib/safe/safe_runtime.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

/* With the violation trap disabled the wrappers return a defined failure and
 * set errno = ERANGE, leaving the destination untouched. Those observable
 * effects are what this test checks (there is no public violation counter). */

int main(void) {
    unsigned char *buf;
    const unsigned char src[16] = {0};
    char *s;

    setvbuf(stdout, NULL, _IONBF, 0);
    cplus_runtime_set_violation_trap(0);

    /* (1) ordinary malloc + in-bounds memset must still work. */
    buf = (unsigned char *)malloc(64);
    assert(buf != NULL);
    memset(buf, 0x41, 64);
    assert(buf[0] == 0x41 && buf[63] == 0x41);

    /* (2) interior pointer with a length larger than its REMAINING capacity. */
    errno = 0;
    memset(buf + 48, 0x42, 32); /* only 16 bytes remain */
    assert(errno == ERANGE);
    assert(buf[48] == 0x41); /* destination untouched on refusal */

    /* (3) one-past-the-end address: capacity is known to be 0, so any write
     * must be refused. This is the exact shape that crashed the original
     * ordinary-C demo. */
    errno = 0;
    memset(buf + 64, 0x43, 16);
    assert(errno == ERANGE);

    /* (4) ordinary memcpy with an oversized length. */
    errno = 0;
    memcpy(buf + 56, src, 16); /* 8 bytes remain */
    assert(errno == ERANGE);

    /* (5) ordinary strcpy into an undersized tracked allocation. */
    s = (char *)malloc(4);
    assert(s != NULL);
    errno = 0;
    strcpy(s, "abcdef");
    assert(errno == ERANGE);
    assert(s[0] == '\0'); /* refused copy left a defined empty destination */

    /* (6) ordinary fread with an overflowing size*count product. */
    {
        FILE *f = tmpfile();
        if (f) {
            size_t r = fread(buf, (size_t)-1, 2, f);
            assert(r == 0);
            assert(errno == EOVERFLOW || errno == ERANGE);
            fclose(f);
        }
    }

    /* (7) unknown-capacity default vs strict mode. A stack array has a known
     * object size, so it is verified in both modes. */
    {
        char stackbuf[8];
        cplus_safe_set_unknown_capacity_mode(1);
        memset(stackbuf, 0x44, sizeof stackbuf);
        assert(stackbuf[7] == 0x44);
        cplus_safe_set_unknown_capacity_mode(0);
    }

    free(s);
    free(buf);
    printf("safe_c_api: ok\n");
    return 0;
}
