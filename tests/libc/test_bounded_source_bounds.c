/* Regressions for issue #89: the plain bounded layer must check the SOURCE
 * buffer as well as the destination, and strncat must not scan an unterminated
 * n-byte source past its end. */
#include "cplus_libc.h"
#include <assert.h>
#include <errno.h>
#include <string.h>

int main(void) {
    char *dst = (char *)cplus_malloc(64);
    char *src = (char *)cplus_malloc(4);
    assert(dst != NULL && src != NULL);

    /* A tracked 4-byte source must refuse a 32-byte copy (over-read). */
    errno = 0;
    assert(cplus_memcpy_bounded(dst, src, 32, 64) == NULL);
    assert(errno == ERANGE);
    errno = 0;
    assert(cplus_memmove_bounded(dst, src, 32, 64) == NULL);
    assert(errno == ERANGE);

    /* One-past-the-end has a known zero-byte capacity. */
    errno = 0;
    assert(cplus_memcpy_bounded(dst, src + 4, 1, 64) == NULL);
    assert(errno == ERANGE);

    /* In-bounds behaviour is unchanged. The source has NO NUL terminator. */
    memcpy(src, "ABCD", 4);
    assert(cplus_memcpy_bounded(dst, src, 4, 64) == dst);
    assert(memcmp(dst, "ABCD", 4) == 0);
    dst[4] = '\0';

    /* strncat scans at most n bytes of the unterminated source. */
    assert(cplus_strncat_bounded(dst, src, 2, 64) == dst);
    assert(memcmp(dst, "ABCDAB", 6) == 0 && dst[6] == '\0');

    cplus_free(dst);
    cplus_free(src);
    return 0;
}
