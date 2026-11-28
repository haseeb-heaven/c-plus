#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include "safe_format.h"
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    char out[64];
    char fmt[32];
    size_t n = size < 31 ? size : 31;
    memcpy(fmt, data, n); fmt[n] = 0;
    for (size_t i=0;i<n;i++) if (fmt[i]=='%') fmt[i]='_';
    (void)cplus_snprintf_s(out, sizeof(out), "f:%s", fmt);
    (void)cplus_sprintf_s(out, sizeof(out), "f:%s", fmt);
    return 0;
}
#ifdef CPLUS_FUZZ_SMOKE_MAIN
int main(void){ const uint8_t *d=(const uint8_t*)"x"; return LLVMFuzzerTestOneInput(d,1); }
#endif
