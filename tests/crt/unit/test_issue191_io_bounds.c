/* Regression for issue #191: the stream-identity caches are fixed-size and
 * compact tombstones in place, so open/close churn cannot grow them into an
 * OOM/EMFILE denial of service; fread_s/fwrite_s fail closed when size*count
 * exceeds bmax (or bmax exceeds RSIZE_MAX_MEM) instead of silently truncating. */
#define _POSIX_C_SOURCE 200112L
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "safe_alloc.h"
#include "safe_io.h"
#include "safe_log.h"

/* Additive diagnostic exported by stdlib/safe/src/safe_io.c.  It is not in the
 * public header, so the test supplies the declaration. */
extern size_t cplus_safe_stream_cache_capacity(void);

int main(void) {
    const char *path = "/tmp/cplus_issue191_io.tmp";
    size_t i;
    FILE *f;
    char *buf;
    char stack_buf[8];

    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);

    /* Thousands of single-file open/close cycles leave <= 1 live entry while
     * accumulating tombstones.  A table that doubled on every compaction would
     * eventually fail calloc() and make every later open/close fail. */
    for (i = 0; i < 10000u; ++i) {
        f = cplus_fopen_s(path, "wb");
        assert(f != NULL);
        assert(cplus_fclose_s(f) == 0);
    }
    assert(cplus_safe_open_stream_count() == 0u);
    assert(cplus_safe_stream_cache_capacity() <= 512u);
    f = cplus_fopen_s(path, "wb+");
    assert(f != NULL);

    /* bmax is the proven allocation size: an oversized request is refused and
     * no byte reaches the stream. */
    buf = (char *)malloc_s(8);
    assert(buf != NULL);
    memset(buf, 'A', 8u);
    errno = 0;
    assert(cplus_fwrite_s(buf, 8u, 1u, 16u, f) == 0u && errno == EINVAL);
    assert(ftell(f) == 0L);
    errno = 0;
    assert(cplus_fread_s(buf, 8u, 1u, 16u, f) == 0u && errno == EINVAL);

    /* Out-of-range bmax is refused for untracked stack/static buffers too. */
    errno = 0;
    assert(cplus_fwrite_s(stack_buf, (size_t)-1, 1u, 1u, f) == 0u && errno == EINVAL);
    errno = 0;
    assert(cplus_fread_s(stack_buf, (size_t)-1, 1u, 1u, f) == 0u && errno == EINVAL);

    /* Multiplication overflow keeps its distinct EOVERFLOW contract. */
    errno = 0;
    assert(cplus_fwrite_s(buf, 8u, 4u, (size_t)-1, f) == 0u && errno == EOVERFLOW);

    /* Positive control: an exact-fit request still succeeds. */
    assert(cplus_fwrite_s(buf, 8u, 1u, 8u, f) == 8u);
    assert(ftell(f) == 8L);

    free_s(buf);
    assert(cplus_fclose_s(f) == 0);
    unlink(path);
    puts("issue191_io_bounds: ok");
    return 0;
}
