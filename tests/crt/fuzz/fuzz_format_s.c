#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "safe_format.h"
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    char fmt[32]; char out[64];
    size_t n = size < 31 ? size : 31;
    memcpy(fmt, data, n); fmt[n] = 0;
    for (size_t i=0;i<n;i++) if (fmt[i]=='%') fmt[i]='_';
    (void)cplus_snprintf_s(out, sizeof(out), "x:%s:%zu", fmt, size);
    return 0;
}
