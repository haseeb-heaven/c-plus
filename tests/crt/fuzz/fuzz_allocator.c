#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include "safe_alloc.h"
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 4) return 0;
    void *p = NULL;
    if (malloc_s(&p, (size_t)data[0] * 16u) == CPLUS_SAFE_OK) {
        ((unsigned char*)p)[0] = data[1];
        (void)realloc_s(&p, (size_t)data[2] * 16u);
        free_s(&p);
    }
    void *c = NULL;
    (void)calloc_s(&c, data[2], data[3] ? data[3] : 1);
    free_s(&c);
    return 0;
}
#ifdef CPLUS_FUZZ_SMOKE_MAIN
int main(void){ uint8_t d[4]={1,2,3,4}; return LLVMFuzzerTestOneInput(d,4); }
#endif
