#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include "safe_alloc.h"
int main(void) {
    void *p = calloc_s(4, 4);
    assert(p != NULL);
    for (int i = 0; i < 16; i++) assert(((unsigned char *)p)[i] == 0);
    free_s(p);
    errno = 0;
    assert(calloc_s((size_t)-1, 2) == NULL && errno == EOVERFLOW);
    errno = 0;
    assert(calloc_s(SIZE_MAX, 1) == NULL && errno == EOVERFLOW);
    puts("unit calloc_s: ok");
    return 0;
}
