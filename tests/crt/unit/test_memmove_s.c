#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "safe_memory.h"
int main(void) {
    char b[16] = "abcdef";
    assert(cplus_memmove_s(b+2, 14, b, 4) == 0); /* overlap */
    assert(memcmp(b, "ababcd", 6) == 0);
    assert(cplus_memmove_s(NULL, 4, b, 1) != 0);
    assert(cplus_memmove_s(b, 2, b, 4) != 0);
    puts("unit memmove_s: ok");
    return 0;
}
