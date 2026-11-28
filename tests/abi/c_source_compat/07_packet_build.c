/* Legacy 07 -- packet serialization (malloc / memset / memcpy at offsets).
 *
 * Builds a 32-byte wire packet by writing through interior pointers. The
 * checked layer must resolve ptr+offset to the REMAINING capacity of the
 * containing heap block, not treat it as an unknown pointer.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    unsigned char *pkt;
    unsigned short len = 0x1234;
    const char *payload = "PING";

    pkt = (unsigned char *)malloc(32);
    if (!pkt) return 2;
    memset(pkt, 0, 32);

    pkt[0] = 0xAB;
    pkt[1] = 0xCD;
    memcpy(pkt + 2, &len, sizeof(len));
    memcpy(pkt + 4, payload, 4);

    printf("packet_build: %02X%02X %04X %c%c%c%c\n",
           (unsigned)pkt[0], (unsigned)pkt[1], (unsigned)len,
           pkt[4], pkt[5], pkt[6], pkt[7]);

    free(pkt);
    return 0;
}
