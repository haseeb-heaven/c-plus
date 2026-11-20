/* Bug 38: side effects evaluated by sizeof on a VLA. */
#include <stdio.h>
int main(void) { int n = 4; size_t s = sizeof(int[n++]); printf("%zu %d\n", s, n); return 0; }
