/* Tests for the dependency-closed safeclib timingsafe integration. */
#include "stdlib/safe/safe_ext_mem.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    static const unsigned char a[16] = {
        0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
    unsigned char b[16];
    memcpy(b, a, sizeof b);

    assert(cplus_sc_timingsafe_bcmp(a, b, sizeof a) == 0);
    assert(cplus_sc_timingsafe_memcmp(a, b, sizeof a) == 0);

    b[15] ^= 0x80;
    assert(cplus_sc_timingsafe_bcmp(a, b, sizeof a) != 0);
    assert(cplus_sc_timingsafe_memcmp(a, b, sizeof a) != 0);

    /* Difference at the last byte must still be detected. */
    b[15] = a[15] ^ 0x01;
    assert(cplus_sc_timingsafe_bcmp(a, b, sizeof a) != 0);
    assert(cplus_sc_timingsafe_memcmp(a, b, sizeof a) != 0);

    /* Zero length is defined and equal. */
    assert(cplus_sc_timingsafe_bcmp(a, b, 0) == 0);
    assert(cplus_sc_timingsafe_memcmp(a, b, 0) == 0);

    /* Sign of memcmp result is meaningful, not the raw magnitude. */
    int r = cplus_sc_timingsafe_memcmp(a, b, sizeof a);
    assert(r < 0 || r > 0);

    puts("timingsafe_s: ok");
    return 0;
}
