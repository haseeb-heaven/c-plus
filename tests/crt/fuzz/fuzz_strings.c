#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "safe_string.h"
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    char dst[64], src[65];
    size_t n = size < 64 ? size : 64;
    memcpy(src, data, n); src[n] = 0;
    size_t dmax = size ? (data[0] % 80) : 0;
    (void)cplus_strcpy_s(dst, dmax, src);
    (void)cplus_strncpy_s(dst, dmax, src, n);
    (void)cplus_strcat_s(dst, dmax, src);
    (void)cplus_strncat_s(dst, dmax, src, n);
    int ind = 0;
    (void)cplus_strcmp_s(dst, dmax, src, &ind);
    (void)cplus_strnlen_s(src, dmax);
    return 0;
}
#ifdef CPLUS_FUZZ_SMOKE_MAIN
int main(void){ const uint8_t *d=(const uint8_t*)"abc"; return LLVMFuzzerTestOneInput(d,3); }
#endif
