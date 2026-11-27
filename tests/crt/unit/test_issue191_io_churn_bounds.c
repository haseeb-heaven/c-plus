/* Regression for issue #191: stream_reserve() must compact tombstones in place
 * instead of doubling the hash table forever, and fread_s/fwrite_s must fail
 * closed when the caller-supplied bmax is the proven buffer capacity. */
#define _POSIX_C_SOURCE 200112L
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "safe_alloc.h"
#include "safe_error.h"
#include "safe_io.h"

/* Additive diagnostic exported by stdlib/safe/src/safe_io.c.  The patch scope
 * does not include the public header, so the test supplies the declaration. */
extern size_t cplus_safe_stream_cache_capacity(void);

int main(void) {
    const char *path = "/tmp/cplus_issue191_io.tmp";
    size_t i;
    FILE *f;
    char *buf;

    /* One live entry, thousands of tombstones.  Before the fix every 75%
     * threshold crossing doubled the table, so this loop allocated ever
     * larger tables (and eventually failed with EMFILE for every call). */
    for (i = 0; i < 10000u; ++i) {
        f = cplus_fopen_s(path, "wb");
        assert(f != NULL);
        assert(cplus_fclose_s(f) == 0);
    }
    assert(cplus_safe_open_stream_count() == 0u);
    assert(cplus_safe_stream_cache_capacity() <= 512u);

    f = cplus_fopen_s(path, "wb+");
    assert(f != NULL);
    buf = (char *)malloc_s(8);
    assert(buf != NULL);
    memset(buf, 'A', 8u);

    /* bmax == proven capacity: size * count > bmax is a refusal, not a
     * silent short I/O.  No byte may reach the stream. */
    errno = 0;
    assert(cplus_fwrite_s(buf, 8u, 1u, 16u, f) == 0u);
    assert(errno == EINVAL);
    assert(ftell(f) == 0L);
    errno = 0;
    assert(cplus_fread_s(buf, 8u, 1u, 16u, f) == 0u);
    assert(errno == EINVAL);

    /* Out-of-range bmax is refused even for an untracked stack object. */
    {
        char stack_buf[8];
        errno = 0;
        assert(cplus_fwrite_s(stack_buf, (size_t)-1, 1u, 1u, f) == 0u);
        assert(errno == EINVAL);
        errno = 0;
        assert(cplus_fread_s(stack_buf, (size_t)-1, 1u, 1u, f) == 0u);
        assert(errno == EINVAL);
    }

    /* Positive control: an exact-fit request still succeeds. */
    assert(cplus_fwrite_s(buf, 8u, 1u, 8u, f) == 8u);
    assert(ftell(f) == 8L);

    free_s(buf);
    assert(cplus_fclose_s(f) == 0);
    unlink(path);
    puts("issue191_io_churn_bounds: ok");
    return 0;
}
