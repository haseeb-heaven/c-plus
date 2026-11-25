#include <stdio.h>
#include <string.h>
#include "safe_memory.h"
int main(void) {
    char dst[16] = {0};
    const char src[] = "C+ safe";
    if (cplus_memcpy_s(dst, sizeof(dst), src, sizeof(src)) != 0) return 1;
    if (strcmp(dst, "C+ safe") != 0) return 1;
    /* boundary: destination too small => nonzero error, no overflow */
    if (cplus_memcpy_s(dst, 3, src, sizeof(src)) == 0) return 1;
    puts("memcpy_s: ok");
    return 0;
}
