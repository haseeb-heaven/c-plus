/* Bug 19: shift count/negative shift. */
#include <stdint.h>
#include <stdio.h>
int main(void) {
    uint32_t x = 1;
    uint32_t bad = x << 32;
    printf("%u\n", bad);
    return 0;
}
