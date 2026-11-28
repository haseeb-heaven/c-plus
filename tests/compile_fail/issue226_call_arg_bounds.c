#include <stddef.h>

/* The interprocedural summaries are keyed by callee name. Calling the
 * two-parameter helper through a one-parameter function pointer used to make
 * the checker index getArg(1) on a one-argument call and crash the compiler.
 * The trailing null dereference proves analysis resumes after the call. */
static void deref_second(int *a, int *b) {
    *b = 1;
}

int main(void) {
    void (*fp)(int *, int *) = deref_second;
    int x = 0;
    ((void (*)(int *))fp)(&x);
    int *p = NULL;
    return *p;
}
