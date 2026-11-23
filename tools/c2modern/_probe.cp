#include "stdlib/error.hp"
#include "stdlib/safe/safe_c.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
int main(void) {
    char dst[32];
    const char *src = "hello";
    memcpy(dst, src, 6);
    strcpy(dst, src);
    void *p = malloc(16);
    if (p) free(p);
    printf("ok %s\n", dst);
    return 0;
}
