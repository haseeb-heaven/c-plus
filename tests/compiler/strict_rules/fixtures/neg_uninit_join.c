/* NEGATIVE definite init: variable read on a path where it was never set. */
#include <stdio.h>
int main(void) { int count; if (count > 0) printf("positive\n"); return 0; }
