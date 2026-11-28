/* POSITIVE heap bounds: index proven below the malloc size at the call. */
#include <stdlib.h>
static int first(int *p) { return p[2]; }
int main(void) { int *a = malloc(16); if (!a) return 2; int v = first(a); free(a); return v == 0 ? 0 : 1; }
