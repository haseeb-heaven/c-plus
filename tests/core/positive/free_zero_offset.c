#include <stdlib.h>
int main(void) { int *p = malloc(sizeof(int)); if (!p) return 1; free(p + 0); return 0; }
