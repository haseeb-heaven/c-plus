/* Managed pointer shadowed in nested scope with early return. */
#include "cplus/cplus.h"
int helper(int which) {
    managed int *x = cplus_new(int);
    *x = 1;
    if (which == 2) {
        managed int *x = cplus_new(int);
        *x = 2;
        print("INNER", *x);
        return 5;
    }
    print("OUTER", *x);
    return 7;
}
int main(void) { print("RET", helper(2)); print("RET2", helper(0)); return 0; }
