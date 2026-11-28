#include <stddef.h>

/* A `flag == 0` early return proves flag non-zero, not p non-null. The null
 * fact for p must survive the guard so the dereference in the next branch is
 * still diagnosed. */
int f(int flag) {
    int *p = NULL;
    if (flag == 0) return 0;
    if (flag > 0)
        return *p;
    return 0;
}

int main(void) { return f(1); }
