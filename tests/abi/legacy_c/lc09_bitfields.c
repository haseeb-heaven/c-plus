/* lc09: bitfields -- mixed-width unsigned fields, truncation on overflow
 * and packing into an aggregate.
 */
#include <stdio.h>

struct flags {
    unsigned int a : 1;
    unsigned int b : 3;
    unsigned int c : 4;
    unsigned int d : 8;
};

int main(void)
{
    struct flags f;
    int ok = 1;

    f.a = 1;
    f.b = 5;
    f.c = 10;
    f.d = 200;
    ok &= (f.a == 1 && f.b == 5 && f.c == 10 && f.d == 200);

    /* 9 does not fit in 3 bits; route through a volatile so the
     * truncation is a runtime value rather than a diagnosed constant. */
    {
        volatile int overflow = 9;
        f.b = overflow;
    }
    ok &= (f.b == 1);

    printf("lc09_bitfields: %s size=%lu v=%u,%u,%u,%u\n",
           ok ? "OK" : "FAIL", (unsigned long)sizeof(struct flags),
           f.a, f.b, f.c, f.d);
    return ok ? 0 : 1;
}
