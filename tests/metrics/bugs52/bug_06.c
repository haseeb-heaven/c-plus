#include <stdlib.h>
int main(void) { int* p = (int*)malloc(sizeof(int)); free(p); *p = 123; return 0; }
