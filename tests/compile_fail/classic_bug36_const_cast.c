/* Bug 36: casting away const to mutate an originally const object. */
#include <stdio.h>
int main(void) { const int x = 10; int *p = (int *)&x; *p = 20; printf("%d\n", x); return 0; }
