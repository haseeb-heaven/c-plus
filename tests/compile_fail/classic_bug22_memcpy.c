/* Bug 22: overlapping memcpy. */
#include <stdio.h>
#include <string.h>
int main(void) { char str[] = "abcdef"; memcpy(str + 2, str, 4); printf("%s\n", str); return 0; }
