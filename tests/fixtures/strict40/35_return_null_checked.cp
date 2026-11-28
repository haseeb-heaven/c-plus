#include <stdlib.h>
int *make_value(void) { int *p = malloc(sizeof *p); if (!p) return 0; *p = 5; return p; }
int main(void) { int *p = make_value(); if (!p) return 1; int result = *p; free(p); return result == 5 ? 0 : 1; }
