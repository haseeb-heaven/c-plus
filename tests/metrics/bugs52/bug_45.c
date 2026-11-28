#include <stdio.h>
int main(void) { float f = 1.0f; int* p = (int*)&f; *p = 0; printf("%f\n", f); return 0; }
