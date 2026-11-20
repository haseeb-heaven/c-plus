/* Bug 12: strict aliasing violation via type punning. */
#include <stdint.h>
#include <stdio.h>
int main(void) {
    float f = 3.14f;
    uint32_t bits = *(uint32_t *)&f;
    printf("0x%x\n", bits);
    return 0;
}
