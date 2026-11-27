/* Regression for issue #207: memcpy_s / memmove_s / memcmp_s must refuse a
 * source capacity (smax) larger than the proven source allocation. */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "safe_alloc.h"
#include "safe_error.h"
#include "safe_memory.h"
#include "safe_runtime.h"

#define ESNOSPC_C 406

int main(void) {
    char *src, *dst;
    int diff = 0;

    cplus_runtime_set_violation_trap(0);
    src = (char *)malloc_s(8);
    dst = (char *)malloc_s(64);
    assert(src != NULL && dst != NULL);
    memset(src, 'A', 8u);
    memset(dst, 0, 64u);

    /* All three direct entry points validate smax before entering safeclib. */
    errno = 0;
    assert(cplus_memcpy_s(dst, 64u, src, 32u) == ESNOSPC_C && errno == ERANGE);
    assert(dst[0] == 0 && dst[63] == 0); /* no copy happened */
    errno = 0;
    assert(cplus_memmove_s(dst, 64u, src, 32u) == ESNOSPC_C && errno == ERANGE);
    assert(dst[0] == 0 && dst[63] == 0);
    errno = 0;
    diff = 0;
    assert(cplus_memcmp_s(dst, 64u, src, 32u, &diff) == ESNOSPC_C && errno == ERANGE);
    assert(diff == -1);
    assert(dst[0] == 0 && dst[63] == 0);

    /* Positive control: smax within the proven source capacity still works. */
    errno = 0;
    assert(cplus_memcpy_s(dst, 64u, src, 8u) == 0);
    assert(memcmp(dst, src, 8u) == 0);
    assert(cplus_memcmp_s(dst, 64u, src, 8u, &diff) == 0 && diff == 0);

    free_s(dst);
    free_s(src);
    puts("issue207_smax_bounds: ok");
    return 0;
}
