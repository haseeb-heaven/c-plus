#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include "safe_alloc.h"
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 3) return 0;
    size_t bytes = (size_t)data[0] * 16u;
    void *p = malloc_s(bytes);
    if (p != NULL) {
        ((unsigned char *)p)[0] = 1;
        void *np = realloc_s(p, (size_t)data[1] * 8u);
        if (np != NULL) p = np;
        free_s(p);
    }
    void *q = calloc_s(data[1], data[2]);
    free_s(q);
    return 0;
}
