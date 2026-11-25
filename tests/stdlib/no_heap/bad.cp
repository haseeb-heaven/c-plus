/* Negative: a raw malloc must be refused in no-heap mode. */
#include "stdlib/no_heap.hp"
#include <stdlib.h>
int main(void) { void *p = malloc(32); free(p); return 0; }
