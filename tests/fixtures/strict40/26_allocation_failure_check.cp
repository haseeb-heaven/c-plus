#include <stdlib.h>
int main(void) { int *p = malloc(sizeof *p); if (!p) return 1; *p = 3; free(p); return 0; }
