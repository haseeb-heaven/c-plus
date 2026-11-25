/* Minimal reproducer for the stdlib_mem_count() large-block defect.
 *
 * Before the fix: a 100000-byte tracked block reports 65535 elements
 * (stdlib_safe_div saturates once the quotient reaches 2^16).
 * After the fix: 100000 elements are reported.
 * Exit 0 = fixed/pass, 1 = defect reproduced.
 */
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/memory.hp"

int main(void) {
    void *p = stdlib_alloc(100000);
    size_t count = 0;
    int rc;
    if (p == (void *)0)
        return 2;
    rc = stdlib_mem_count(p, 1, &count);
    printf("stdlib_mem_count(100000-byte block, elem_size=1) -> rc=%d count=%zu\n",
           rc, count);
    stdlib_release(p);
    return (rc == 0 && count == 100000) ? 0 : 1;
}
