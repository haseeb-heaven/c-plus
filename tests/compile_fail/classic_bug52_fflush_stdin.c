/* Bug 52: fflush(stdin) is undefined. */
#include <stdio.h>
int main(void) { int age; scanf("%d",&age); fflush(stdin); return 0; }
