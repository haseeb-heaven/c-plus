/* lc16: alignment-sensitive structs -- a naturally padded record whose
 * fields have different alignment requirements; verify offsets and a raw
 * byte-for-byte round trip through memcpy.
 */
#include <stddef.h>
#include <stdio.h>
#include <string.h>

struct align_rec {
    char c;
    double d;
    short s;
};

int main(void)
{
    struct align_rec r;
    struct align_rec copy;
    int ok = 1;

    r.c = 'q';
    r.d = 3.5;
    r.s = 77;
    memcpy(&copy, &r, sizeof(r));

    ok &= (copy.c == 'q' && copy.d == 3.5 && copy.s == 77);
    ok &= (memcmp(&r, &copy, sizeof(r)) == 0);
    ok &= (offsetof(struct align_rec, c) < offsetof(struct align_rec, d));
    ok &= (offsetof(struct align_rec, d) < offsetof(struct align_rec, s));
    ok &= (sizeof(struct align_rec) > sizeof(double) + sizeof(short) + 1u);

    printf("lc16_alignment: %s size=%lu off=%lu,%lu,%lu vals=%c,%.1f,%d\n",
           ok ? "OK" : "FAIL", (unsigned long)sizeof(struct align_rec),
           (unsigned long)offsetof(struct align_rec, c),
           (unsigned long)offsetof(struct align_rec, d),
           (unsigned long)offsetof(struct align_rec, s),
           copy.c, copy.d, copy.s);
    return ok ? 0 : 1;
}
