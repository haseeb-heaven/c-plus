#include <string.h>
int main(void) { char d[8]; char *s = 0; memcpy(d, s, 4); return 0; }  // NOSONAR intentional unsafe fixture for the C+ safe-mode checker
