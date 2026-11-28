/* Unit: checked-allocation overflow safety (bugs 105/107). */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "safe_alloc.h"
#include "safe_error.h"
#include "safe_log.h"

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);

    errno = 0; assert(calloc_s((size_t)-1, 4) == NULL && errno == EOVERFLOW);
    errno = 0; assert(calloc_s(SIZE_MAX / 4 + 1, 4) == NULL && errno == EOVERFLOW);
    errno = 0; assert(calloc_s(2, SIZE_MAX / 2 + 1) == NULL && errno == EOVERFLOW);
    errno = 0; assert(calloc_s(SIZE_MAX, 1) == NULL && errno == EOVERFLOW);
    errno = 0; assert(calloc_s(1, (size_t)-1) == NULL && errno == EOVERFLOW);
    errno = 0; assert(calloc_s(RSIZE_MAX_MEM, 1) == NULL && errno == ENOMEM);

    void *z = calloc_s(0, 0);
    assert(z != NULL);
    free_s(z);
    z = calloc_s(4, 0);
    assert(z != NULL);
    free_s(z);

    void *q = calloc_s(4, 8);
    assert(q != NULL);
    for (int i = 0; i < 32; ++i) assert(((unsigned char *)q)[i] == 0);
    free_s(q);

    errno = 0; assert(malloc_s((size_t)-1) == NULL && errno == EINVAL);
    errno = 0; assert(malloc_s(RSIZE_MAX_MEM) == NULL && errno == ENOMEM);
    void *m = malloc_s(0);
    assert(m != NULL);
    assert(free_checked_s(m) == CPLUS_SAFE_OK);
    assert(free_checked_s(m) == CPLUS_SAFE_EINVAL);
    free_s(NULL);

    puts("test_calloc_s_overflow_uninit: ok");
    return 0;
}
