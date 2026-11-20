/* Bug 48: _BitInt has no safe lowering. */
#include <stdio.h>
int main(void) { _BitInt(4) a = 5, b = 5; _BitInt(4) s = a + b; printf("%d\n", (int)s); return 0; }
