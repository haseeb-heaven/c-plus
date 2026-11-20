/* Bug 42: realloc(ptr, 0) is UB in C23.  C+ rejects raw realloc entirely. */
#include <stdlib.h>
int main(void) { int *b = malloc(16); if (!b) return 1; int *r = realloc(b, 0); free(r); return 0; }
