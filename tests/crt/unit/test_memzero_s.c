#include <assert.h>
#include <stdio.h>
#include "safe_memory.h"
int main(void) {
    char b[8] = "secret!";
    assert(cplus_memzero_s(b, sizeof(b)) == 0);
    for (int i=0;i<8;i++) assert(b[i]==0);
    assert(cplus_memzero_s(NULL, 8) != 0);
    puts("unit memzero_s: ok");
    return 0;
}
