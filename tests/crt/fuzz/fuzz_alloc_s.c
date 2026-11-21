#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include "safe_alloc.h"
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 3) return 0;
    void *p = NULL;
    size_t bytes = (size_t)data[0] * 16u;
    if (malloc_s(&p, bytes) == CPLUS_SAFE_OK) {
        ((unsigned char*)p)[0] = 1;
        (void)realloc_s(&p, (size_t)data[1] * 8u);
        free_s(&p);
    }
    void *q = NULL;
    (void)calloc_s(&q, data[1], data[2]);
    free_s(&q);
    return 0;
}
