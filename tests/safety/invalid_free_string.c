#include <stdlib.h>
int main(void) { const char *s = "hello"; free((void *)s); return 0; }  // NOSONAR intentional unsafe fixture for the C+ safe-mode checker
