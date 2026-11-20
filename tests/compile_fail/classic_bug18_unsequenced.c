/* Bug 18: unsequenced modification. */
#include <stdio.h>
int main(void) { int i = 1; int r = i++ + ++i; printf("%d\n", r); return 0; }
