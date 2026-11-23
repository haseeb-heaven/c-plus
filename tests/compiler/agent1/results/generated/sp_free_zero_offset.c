#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/core/positive/free_zero_offset.c"
#include <stdlib.h>
int main(void) { int *p = malloc(sizeof(int)); if (!p) return 1; free(p + 0); return 0; }