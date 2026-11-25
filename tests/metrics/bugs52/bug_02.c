#include <stdlib.h>
int main(void) { int* p = (int*)malloc((size_t)-1); *p = 10; free(p); return 0; }
