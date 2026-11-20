/* Bug 21: unaligned pointer dereference. */
#include <stdint.h>
#include <stdio.h>
int main(void) {
    uint8_t buffer[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    uint32_t *p = (uint32_t *)(buffer + 1);
    printf("%u\n", *p);
    return 0;
}
