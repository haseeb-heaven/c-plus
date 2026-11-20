/* Bug 23: safe alternative — copy the whole allocation explicitly. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct Packet { int length; char payload[]; };
int main(void) {
    size_t total = sizeof(struct Packet) + 4;
    struct Packet *p1 = malloc(total);
    p1->length = 4;
    memcpy(p1->payload, "DATA", 4);
    struct Packet *p2 = malloc(total);
    memcpy(p2, p1, total);
    printf("%.*s\n", p2->length, p2->payload);
    free(p1); free(p2);
    return 0;
}
