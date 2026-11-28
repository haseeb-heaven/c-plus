#include <stdio.h>
int main(void) { int* p = NULL; if (1) { p = (int[]){10, 20, 30}; } printf("%d\n", p[0]); return 0; }
