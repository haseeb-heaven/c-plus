#include <stdlib.h>
int main(void) { int x = 10; int* p = &x; free(p); return 0; }
