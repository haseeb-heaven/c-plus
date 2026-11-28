/* lc15: packed structures -- an explicitly packed record whose fields have
 * no inter-field padding; checks the compact layout and field round trip.
 *
 * __attribute__((packed)) is the conventional C ABI extension for this and
 * is accepted by the host C compiler without pedantic diagnostics.
 */
#include <stdio.h>

struct packed_rec {
    unsigned char a;
    unsigned int b;
    unsigned short c;
} __attribute__((packed));

int main(void)
{
    struct packed_rec r;
    int ok = 1;

    r.a = 1;
    r.b = 0x01020304u;
    r.c = 0x0506u;
    ok &= (r.a == 1);
    ok &= (r.b == 0x01020304u);
    ok &= (r.c == 0x0506u);
    ok &= (sizeof(struct packed_rec) == 1u + 4u + 2u);

    printf("lc15_packed_struct: %s size=%lu a=%u b=%08x c=%04x\n",
           ok ? "OK" : "FAIL", (unsigned long)sizeof(struct packed_rec),
           (unsigned int)r.a, (unsigned int)r.b, (unsigned int)r.c);
    return ok ? 0 : 1;
}
