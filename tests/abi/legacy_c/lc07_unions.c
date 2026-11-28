/* lc07: unions -- shared storage for int/double/byte views, size and
 * active-member round trips.
 */
#include <stdio.h>

union number {
    int i;
    double d;
    unsigned char bytes[8];
};

struct tagged {
    int tag;
    union number u;
};

int main(void)
{
    union number ni;
    union number nd;
    struct tagged rec;
    int ok = 1;

    ni.i = 42;
    nd.d = 2.5;
    ok &= (ni.i == 42 && nd.d == 2.5);

    rec.tag = 7;
    rec.u.i = 1234;
    ok &= (rec.tag == 7 && rec.u.i == 1234);
    ok &= (sizeof(union number) >= sizeof(double));
    ok &= (sizeof(struct tagged) >= sizeof(union number));

    printf("lc07_unions: %s i=%d d=%.1f tag=%d sz=%lu\n",
           ok ? "OK" : "FAIL", ni.i, nd.d, rec.tag,
           (unsigned long)sizeof(union number));
    return ok ? 0 : 1;
}
