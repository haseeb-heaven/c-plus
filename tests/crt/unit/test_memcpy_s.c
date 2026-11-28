#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "safe_memory.h"
int main(void) {
    char dst[16] = {0};
    const char src[] = "abc";
    assert(cplus_memcpy_s(dst, sizeof(dst), src, 4) == 0);
    assert(strcmp(dst, "abc") == 0);
    assert(cplus_memcpy_s(NULL, 16, src, 4) != 0);
    assert(cplus_memcpy_s(dst, 0, src, 4) != 0);
    assert(cplus_memcpy_s(dst, 2, src, 4) != 0); /* too small */
    puts("unit memcpy_s: ok");
    return 0;
}
