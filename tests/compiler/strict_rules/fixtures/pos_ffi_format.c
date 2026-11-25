/* POSITIVE FFI: untrusted text is a %s argument, never the format string. */
#include <stdio.h>
int main(void) { char user[] = "%x %x"; printf("%s\n", user); return 0; }
