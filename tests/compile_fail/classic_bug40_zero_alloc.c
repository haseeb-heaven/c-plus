/* Bug 40: zero-size realloc. */
#include <stdlib.h>
int main(void) { int *b = malloc(16); if (!b) return 1; int *r = realloc(b, 0); free(r); return 0; }
