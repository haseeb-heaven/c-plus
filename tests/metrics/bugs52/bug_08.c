#include <stdio.h>
int main(void) { int* p = NULL; { int temp = 77; p = &temp; } printf("%d\n", *p); return 0; }
