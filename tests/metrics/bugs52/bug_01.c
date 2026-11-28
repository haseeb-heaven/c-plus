#include <stdlib.h>
int main(void) { int* p = (int*)malloc(sizeof(int)); if (p) *p = 42; return 0; }
