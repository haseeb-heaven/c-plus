#include <stdlib.h>
#include <stdio.h>
int main(void) { int* p = (int*)malloc(sizeof(int)); *p = 99; free(p); printf("%d\n", *p); return 0; }
