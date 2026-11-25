/*
 * lc48 - struct patterns: C99 flexible array member allocated in one block.
 *
 * Independent legacy-C ABI probe: a packet header followed by a variable
 * length payload; verifies size arithmetic and payload integrity.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct packet {
    unsigned long length;
    unsigned int kind;
    unsigned char data[];
};

int main(void)
{
    const unsigned char payload[6] = { 2, 4, 6, 8, 10, 12 };
    struct packet *p;
    size_t i;
    int ok = 1;

    p = (struct packet *)malloc(sizeof(*p) + 6u * sizeof(unsigned char));
    if (p == NULL) {
        printf("lc48 result: FAIL malloc\n");
        return 1;
    }
    p->length = 6UL;
    p->kind = 48u;
    for (i = 0; i < 6u; ++i) {
        p->data[i] = payload[i];
    }
    if (p->length != 6UL || p->kind != 48u) {
        ok = 0;
    }
    for (i = 0; i < 6u; ++i) {
        if (p->data[i] != payload[i]) {
            ok = 0;
        }
    }
    if (sizeof(*p) > sizeof(unsigned long) + sizeof(unsigned int) + 8u) {
        ok = 0; /* header must not accidentally include payload storage */
    }
    free(p);
    if (!ok) {
        printf("lc48 result: FAIL payload check\n");
        return 1;
    }
    printf("lc48 result: OK header=%lu len=6 kind=48\n",
           (unsigned long)sizeof(struct packet));
    return 0;
}
