/* Bug 695: type-punned dereference of a double through a uint64_t lvalue. */
#include <stdint.h>
#include <stdio.h>

int main(void) {
    double reading = 42.5;
    uint64_t raw = *(uint64_t *)&reading;
    printf("raw=%llx\n", (unsigned long long)raw);
    return 0;
}
