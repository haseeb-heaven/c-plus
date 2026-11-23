#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/c2y_ifdecl.cp"
#include "cplus/cplus.h"
int main(void) {
    { int x = 5; if (x > 3) {
        print("big", x);
    } else {
        print("small", x);
    }}
    return 0;
}
