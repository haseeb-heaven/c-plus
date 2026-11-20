/* Bug 25: division by zero. */
#include <stdio.h>
int main(void) { int z = 0; int r = 1 / z; printf("%d\n", r); return 0; }
