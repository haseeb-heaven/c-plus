/*
 * lc43 - struct patterns: bitfields, including zero-width alignment.
 *
 * Independent legacy-C ABI probe: exercises bit-field packing and assignment
 * truncation using implementation-defined but deterministic widths.
 */
#include <stdio.h>

struct flags {
    unsigned int ready : 1;
    unsigned int mode  : 3;
    unsigned int count : 4;
    unsigned int       : 0;   /* next field starts on a new storage unit */
    unsigned int extra : 8;
};

int main(void)
{
    struct flags f;
    unsigned int raw_mode, raw_count;

    f.ready = 1u;
    f.mode = 5u;
    f.count = 12u;
    f.extra = 0xA5u;
    raw_mode = f.mode;
    raw_count = f.count;

    if (f.ready != 1u || raw_mode != 5u || raw_count != 12u ||
        f.extra != 0xA5u) {
        printf("lc43 result: FAIL ready=%u mode=%u count=%u extra=%u\n",
               f.ready, f.mode, f.count, f.extra);
        return 1;
    }
    /* Verify modulo truncation on overflow of the 3-bit field.
       Use a non-constant source so the compiler does not diagnose the
       deliberate narrowing at compile time. */
    {
        unsigned int wrap = 9u;
        f.mode = wrap;
    }
    if (f.mode != 1u) {
        printf("lc43 result: FAIL wrap mode=%u\n", f.mode);
        return 1;
    }
    printf("lc43 result: OK mode=%u count=%u extra=%02x size=%lu\n",
           raw_mode, raw_count, f.extra, (unsigned long)sizeof(f));
    return 0;
}
