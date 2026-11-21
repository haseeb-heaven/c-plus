#include <assert.h>
#include <stdio.h>
#include "safe_memory.h"
int main(void) {
    unsigned char b[8];
    assert(cplus_memset_s(b, sizeof(b), 0x7f, sizeof(b)) == 0);
    assert(b[0]==0x7f && b[7]==0x7f);
    assert(cplus_memset_s(NULL, 8, 0, 1) != 0);
    unsigned char before4 = b[4];
    (void)cplus_memset_s(b, 4, 0, 5); /* n>dmax: must not write past dmax */
    assert(b[4] == before4);
    puts("unit memset_s: ok");
    return 0;
}
