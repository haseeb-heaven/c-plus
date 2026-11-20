/* Bug 27: relational comparison of unrelated pointers. */
#include <stdio.h>
int main(void) { int a = 10, b = 20; if (&a < &b) printf("yes\n"); return 0; }
