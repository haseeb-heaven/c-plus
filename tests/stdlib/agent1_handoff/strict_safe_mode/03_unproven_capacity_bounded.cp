// OP: unproven capacity passed to a bounded wrapper.
// cplus_memcpy_s is the bounded (dmax/smax) wrapper. The capacities come from
// a function parameter, i.e. values the compiler cannot prove against the
// actual object sizes at the call site.
#include "cplus/cplus.h"
#include "../../../../stdlib/safe/safe_memory.h"

errno_t copy_block(char *dst, size_t dst_cap,
                   const char *src, size_t src_cap, size_t n) {
    return cplus_memcpy_s(dst, dst_cap, src, src_cap); /* capacities unproven */
}

int main(void) {
    char a[8];
    char b[8] = {0};
    (void)copy_block(a, sizeof(a), b, sizeof(b), 8);
    return 0;
}
