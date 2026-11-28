#include <stdlib.h>
static int g;  // NOSONAR intentional unsafe fixture for the C+ safe-mode checker
int main(void) { free(&g); return 0; }
