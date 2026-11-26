/* Independent modern-C port of the "memory" the inspiration language sample workload.
 * C11, no the inspiration language source copied. See proof/modern_c_ports/README.md. */
#include "common.h"

int main(void) {
    unsigned char *arena = (unsigned char *)malloc(256);
    unsigned char *top = arena;
    int i;
    if (!arena) return 1;
    for (i = 0; i < 8; ++i) { int *p = (int *)top; *p = i * i; top += sizeof(int); }
    CHECK("memory_bump", (size_t)(top - arena) == 8u * sizeof(int));
    printf("memory last %d\n", ((int *)arena)[7]);
    free(arena); report("MEMORY"); return fails;
}
