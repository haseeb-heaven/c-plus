/* Bug 53: unbounded VLA / alloca stack allocation. */
#include <stdio.h>
void f(size_t n) { int buf[n]; buf[0]=1; printf("%d\n",buf[0]); }
int main(void) { f(100); return 0; }
