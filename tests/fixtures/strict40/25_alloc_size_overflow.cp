#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>
int main(void) { size_t count = SIZE_MAX / 2 + 1; size_t total = count * 2; void *p = malloc(total); free(p); return 0; }
