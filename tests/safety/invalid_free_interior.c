#include <stdlib.h>
int main(void) { int *p = malloc(100 * sizeof(int)); if (!p) return 1; free(p + 1); return 0; }
