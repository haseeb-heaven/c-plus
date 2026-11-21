#include <string.h>
int main(void) { char d[4]; memset(d, 0, 10); return 0; }  // NOSONAR intentional unsafe fixture for the C+ safe-mode checker
