#include <stdlib.h>
int main(void) { char a[16] = {0}; free(a); return 0; }  // NOSONAR intentional unsafe fixture for the C+ safe-mode checker
