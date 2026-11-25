/* NEGATIVE runtime bounds: index exceeds the tracked allocation; static proof is unavailable. */
#include <stdlib.h>
int main(int argc, char **argv) { (void)argv; int *p = malloc(4 * sizeof(int)); if (!p) return 2; int i = argc + 100; p[i] = 7; free(p); return 0; }
