#include <stddef.h>
extern size_t __cplus_bounds_check(size_t, size_t, const char *);
#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/multidim_bounds.c"
/* Valid: i/4 in 0..3 and i%4 in 0..3 for a 4x4 array. */
static int g[4][4];
int main(void) {
    for (int i = 0; i < 16; i++)
        g[__cplus_bounds_check((size_t)(i/4), 4, "g")][i%4] = i;
    int r[4];                 /* declaration inside loop must not be checked */
    for (int i = 0; i < 4; i++)
        r[__cplus_bounds_check((size_t)(i), 4, "r")] = g[0][i];
    return r[0];
}