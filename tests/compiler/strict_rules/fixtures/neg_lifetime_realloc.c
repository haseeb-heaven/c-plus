/* NEGATIVE realloc: raw realloc may lose the original block on failure. */
#include <stdlib.h>
int main(void) { int *a = malloc(40); if (!a) return 1; a = realloc(a, 1000); free(a); return 0; }
