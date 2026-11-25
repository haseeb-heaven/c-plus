#include <stdlib.h>
struct Array { int* items; };
int main(void) { struct Array a; a.items = (int*)malloc(4 * sizeof(int)); struct Array b = a; free(a.items); free(b.items); return 0; }
