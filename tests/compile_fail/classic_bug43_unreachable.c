/* Bug 43: C23 unreachable() cascading optimization. */
#include <stddef.h>
#include <stdio.h>
int handle(int m) { switch (m) { case 1: return 10; default: unreachable(); } }
int main(void) { printf("%d\n", handle(3)); return 0; }
