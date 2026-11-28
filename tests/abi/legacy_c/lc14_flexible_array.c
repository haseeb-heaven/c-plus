/* lc14: flexible array members -- a C99 struct whose last member is an
 * unsized array allocated in one block with its payload.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct packet {
    unsigned int len;
    unsigned char data[];
};

int main(void)
{
    size_t n = 4;
    struct packet *p = (struct packet *)malloc(sizeof(struct packet) + n);
    int ok = (p != NULL);

    if (ok) {
        p->len = (unsigned int)n;
        memcpy(p->data, "ABCD", n);
        ok &= (p->len == 4);
        ok &= (p->data[0] == 'A' && p->data[3] == 'D');
        printf("lc14_flexible_array: %s len=%u bytes=%c%c%c%c hdr=%lu\n",
               ok ? "OK" : "FAIL", p->len,
               p->data[0], p->data[1], p->data[2], p->data[3],
               (unsigned long)sizeof(struct packet));
        free(p);
    } else {
        printf("lc14_flexible_array: FAIL allocation\n");
    }
    return ok ? 0 : 1;
}
