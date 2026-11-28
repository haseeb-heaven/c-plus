/* Bug 34: longjmp bypasses cleanup and clobbers non-volatile locals. */
#include <setjmp.h>
#include <stdlib.h>
jmp_buf env;
void work(void) { char *t = malloc(256); longjmp(env, 1); free(t); }
int main(void) { int state = 1; if (setjmp(env) == 0) { state = 2; work(); } return state; }
