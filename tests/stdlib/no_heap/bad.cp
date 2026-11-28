/* Negative: a raw malloc must be refused in no-heap mode. */
#include <stdlib.h>
#include "stdlib/no_heap.hp"  /* after system headers, before user code */
int main(void) { void *p = malloc(32); free(p); return 0; }
