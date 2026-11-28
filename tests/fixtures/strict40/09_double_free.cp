#include <stdlib.h>
int main(void) { int *p = malloc(sizeof *p); if (!p) return 1; free(p); free(p); return 0; }
