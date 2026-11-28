#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "safe_memory.h"
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 8) return 0;
    char dst[64], src[64];
    memset(dst, 0xAA, sizeof(dst));
    size_t n = data[0] % 65;
    size_t dmax = data[1] % 80;
    size_t copy = size - 2 < sizeof(src) ? size - 2 : sizeof(src);
    memcpy(src, data + 2, copy);
    (void)cplus_memcpy_s(dst, dmax, src, n);
    (void)cplus_memmove_s(dst, dmax, src, n);
    (void)cplus_memset_s(dst, dmax, (int)data[2], n);
    int diff = 0;
    (void)cplus_memcmp_s(dst, dmax, src, n, &diff);
    (void)cplus_memzero_s(dst, dmax);
    return 0;
}
#ifdef CPLUS_FUZZ_SMOKE_MAIN
int main(void){ uint8_t d[16]={0}; for(int i=0;i<16;i++)d[i]=(uint8_t)i; return LLVMFuzzerTestOneInput(d,16); }
#endif
