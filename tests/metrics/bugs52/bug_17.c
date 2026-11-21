#include <setjmp.h>
#include <stdlib.h>
jmp_buf env;
void deep_func(void) { int* temp = (int*)malloc(256); longjmp(env, 1); free(temp); }
int main(void) { if (setjmp(env) == 0) { deep_func(); } return 0; }
