/* POSITIVE runtime bounds: dynamically computed index proven in range by the emitted guard. */
#include <stdlib.h>
int main(int argc, char **argv) { (void)argv; int *p = malloc(4 * sizeof(int)); if (!p) return 2; int i = argc - 1; p[i] = 7; int v = p[i]; free(p); return v == 7 ? 0 : 1; }
