/* NEGATIVE FFI: non-literal format string. */
#include <stdio.h>
int main(void) { char user[] = "%x %x %s"; printf(user); return 0; }
