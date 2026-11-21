#include <stdlib.h>
int main(void) { int* p = (int*)malloc(5 * sizeof(int)); p++; free(p); return 0; }
