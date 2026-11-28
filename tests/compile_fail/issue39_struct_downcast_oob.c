/* Issue #39: a pointer to a small heap allocation is downcast to a larger
 * struct type, then its fields are written past the allocation.  The cast
 * must be rejected because the allocation extent cannot cover the target. */
#include <stdio.h>
#include <stdlib.h>

struct Header {
    int id; /* 4 bytes */
};

struct LargePacket {
    int id;
    int payload[4]; /* 20 bytes total */
};

int main(void) {
    struct Header *hdr = (struct Header *)malloc(sizeof(struct Header));
    if (!hdr) return 1;
    hdr->id = 1;
    struct LargePacket *pkt = (struct LargePacket *)(void *)hdr;
    pkt->payload[3] = 0x1337; /* out-of-bounds heap write */
    printf("[STRUCT DOWNCAST] Wrote past allocation: 0x%x\n", pkt->payload[3]);
    free(hdr);
    return 0;
}
