#include <stdio.h>
#include <errno.h>
#include "safe_memory.h"
int main(void) {
    unsigned char b[8];
    if (cplus_memset_s(b, sizeof(b), 0xAA, sizeof(b)) != 0) return 1;
    if (b[0] != 0xAA || b[7] != 0xAA) return 1;
    /* n > dmax: contract violation. Upstream behavior is implementation-defined;
       safe contract is "do not overflow dmax". We verify no bytes beyond dmax changed. */
    unsigned char before4 = b[4];
    (void)cplus_memset_s(b, 4, 0, 8);
    if (b[4] != before4) return 1;
    puts("memset_s: ok");
    return 0;
}
