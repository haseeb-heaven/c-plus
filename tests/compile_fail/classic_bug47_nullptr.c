/* Bug 47: C23 nullptr is not available in C+ (gnu17). */
#include <stddef.h>
int main(void) { void *p = nullptr; (void)p; return 0; }
