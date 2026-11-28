/* lc08: nested structs -- inner records embedded in outer records, checked
 * with offsetof and accessed through nested member chains.
 */
#include <stddef.h>
#include <stdio.h>

struct inner {
    int a;
    char b;
};

struct outer {
    char tag;
    struct inner in;
    double d;
};

int main(void)
{
    struct outer o;
    struct outer *p = &o;
    int ok = 1;

    o.tag = 'x';
    o.in.a = 7;
    o.in.b = 'z';
    o.d = 1.5;

    ok &= (p->in.a == 7 && p->in.b == 'z');
    ok &= (offsetof(struct outer, in) >= sizeof(char));
    ok &= (offsetof(struct outer, d) > offsetof(struct outer, in));
    ok &= ((const char *)&o.in.a - (const char *)&o ==
           (long)offsetof(struct outer, in));

    printf("lc08_nested_structs: %s size=%lu off_in=%lu off_d=%lu val=%d%c\n",
           ok ? "OK" : "FAIL", (unsigned long)sizeof(struct outer),
           (unsigned long)offsetof(struct outer, in),
           (unsigned long)offsetof(struct outer, d),
           p->in.a, p->in.b);
    return ok ? 0 : 1;
}
