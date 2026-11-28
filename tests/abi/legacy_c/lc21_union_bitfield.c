/* lc21: union overlay of a bitfield record and its raw integer storage --
 * exercises the ABI interaction between bit packing and union sharing.
 */
#include <stdio.h>

union bits_view {
    struct {
        unsigned int low : 4;
        unsigned int high : 4;
        unsigned int flag : 1;
    } f;
    unsigned int raw;
};

int main(void)
{
    union bits_view v;
    int ok = 1;

    v.raw = 0u;
    v.f.low = 0xAu;
    v.f.high = 0x3u;
    v.f.flag = 1u;
    ok &= (v.f.low == 0xAu && v.f.high == 0x3u && v.f.flag == 1u);
    ok &= (sizeof(union bits_view) >= sizeof(unsigned int));

    printf("lc21_union_bitfield: %s low=%u high=%u flag=%u raw=%u size=%lu\n",
           ok ? "OK" : "FAIL", v.f.low, v.f.high, v.f.flag,
           v.raw, (unsigned long)sizeof(union bits_view));
    return ok ? 0 : 1;
}
