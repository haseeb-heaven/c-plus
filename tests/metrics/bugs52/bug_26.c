#include <stdlib.h>
int main(void) { int* buf = (int*)malloc(10 * sizeof(int)); for (int i = 0; i <= 10; i++) { buf[i] = i; } free(buf); return 0; }
