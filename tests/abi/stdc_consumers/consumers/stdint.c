/* Consumer: <stdint.h> fixed-width types and limit macros. */
#include <stdint.h>
int stdc_stdint_probe(void)
{
    uint32_t u = UINT32_C(0x10);
    int32_t s = INT32_C(-3);
    uint64_t w = UINT64_C(1) << 40;
    return (int)(u + (uint32_t)(s < 0) + (uint32_t)(w != 0));
}
