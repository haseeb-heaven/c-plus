#include <stdlib.h>
int main(void) { int x = 0; int *p = &x; free(p); return 0; }  // NOSONAR intentional unsafe fixture for the C+ safe-mode checker
