#include <stddef.h>
#include <stdlib.h>
int main(void) { size_t n = (size_t)-1 / 2 + 2; int* p = (int*)malloc(n * sizeof(int)); if (p) { p[0] = 1; free(p); } return 0; }
