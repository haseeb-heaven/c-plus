/* NEGATIVE function boundary: caller passes null into a dereferencing callee. */
#include <stddef.h>
static void deref(int *p) { *p = 1; }
int main(void) { deref(NULL); return 0; }
