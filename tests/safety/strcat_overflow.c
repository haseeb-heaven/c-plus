#include <string.h>
int main(void) { char d[4] = "a"; strcat(d, "abcdefgh"); return 0; }  // NOSONAR intentional unsafe fixture for the C+ safe-mode checker
