/* NOT DETECTED: copying a struct with a flexible array member loses payload. */
#include <stdlib.h>
#include <string.h>
struct Packet { int length; char payload[]; };
int main(void) {
    struct Packet *p1 = malloc(sizeof(struct Packet) + 4);
    p1->length = 4;
    memcpy(p1->payload, "DATA", 4);
    struct Packet *p2 = malloc(sizeof(struct Packet));
    *p2 = *p1;
    free(p1); free(p2);
    return 0;
}
