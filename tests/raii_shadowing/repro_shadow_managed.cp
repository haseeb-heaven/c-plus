/* Managed pointer shadowed in nested scope; both must be released exactly once. */
#include "cplus/cplus.h"
int main(void) {
    managed int *x = cplus_new(int);
    *x = 1;
    {
        managed int *x = cplus_new(int);
        *x = 2;
        print("INNER", *x);
    }
    print("OUTER", *x);
    return 0;
}
