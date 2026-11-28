/* lc20: struct by value -- returning aggregates from functions and passing
 * them through a chain of calls (register/stack ABI sensitive).
 */
#include <stdio.h>

struct pair {
    long a;
    long b;
};

static struct pair make_pair(long x, long y)
{
    struct pair p;
    p.a = x;
    p.b = y;
    return p;
}

static struct pair fold(struct pair p)
{
    p.a += p.b;
    p.b *= 2;
    return p;
}

int main(void)
{
    struct pair p = make_pair(100, 23);
    struct pair r = fold(p);
    int ok = 1;

    ok &= (p.a == 100 && p.b == 23);
    ok &= (r.a == 123 && r.b == 46);
    ok &= (sizeof(struct pair) == 2u * sizeof(long));

    printf("lc20_struct_return: %s in=%ld,%ld out=%ld,%ld size=%lu\n",
           ok ? "OK" : "FAIL", p.a, p.b, r.a, r.b,
           (unsigned long)sizeof(struct pair));
    return ok ? 0 : 1;
}
