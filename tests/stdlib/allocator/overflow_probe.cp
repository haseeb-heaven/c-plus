/* scratch probe: SafeCRT link from a .cp test */
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/safe/safe_error.h"
#include "stdlib/safe/safe_alloc.h"

int main(void) {
    long s0 = cplus_safe_alloc_seq();
    errno = 0;
    print("probe_aligned_bad", aligned_alloc_s(3u, 16u) == NULL && errno == EINVAL);
    print("probe_aligned_ovf", aligned_alloc_s(16u, SIZE_MAX) == NULL && errno == EINVAL);
    print("probe_aligned_seq", cplus_safe_alloc_seq() == s0);
    print("probe_calloc_ovf", calloc_s(SIZE_MAX, 4u) == NULL && errno == EOVERFLOW);
    print("probe_calloc_seq", cplus_safe_alloc_seq() == s0);
    return 0;
}
