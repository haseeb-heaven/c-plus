#include <stdlib.h>
int main(void) { int *p = malloc(sizeof *p); if (!p) return 1; *p = 9; free(p); return *p; }
