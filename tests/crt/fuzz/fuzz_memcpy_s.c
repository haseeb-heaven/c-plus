#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "safe_memory.h"
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 4) return 0;
    char dst[64]; char src[64];
    memset(dst, 0xAA, sizeof(dst));
    size_t dmax = data[0] % 80;
    size_t smax = data[1] % 80;
    size_t n = data[2] % sizeof(src);
    memcpy(src, data + 3, size - 3 < sizeof(src) ? size - 3 : sizeof(src));
    (void)cplus_memcpy_s(dst, dmax, src, smax);
    (void)cplus_memcpy_s(dst, sizeof(dst), src, n < sizeof(src) ? n : sizeof(src));
    return 0;
}
