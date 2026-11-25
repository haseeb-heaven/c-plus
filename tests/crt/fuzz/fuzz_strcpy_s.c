#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "safe_string.h"
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    char dst[64]; char src[65];
    size_t n = size < 64 ? size : 64;
    memcpy(src, data, n); src[n] = 0;
    size_t dmax = size ? (data[0] % 80) : 0;
    (void)cplus_strcpy_s(dst, dmax, src);
    (void)cplus_strncpy_s(dst, dmax, src, n);
    return 0;
}
