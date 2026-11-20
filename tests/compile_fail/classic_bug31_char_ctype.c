/* Bug 31: plain char passed to <ctype.h>. */
#include <ctype.h>
#include <stdio.h>
int main(void) { char ch = (char)0xE9; if (isalpha(ch)) printf("alpha\n"); return 0; }
