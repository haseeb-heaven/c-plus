#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/c2y_defer.cp"
#include "cplus/cplus.h"

static int process(int early) {
    print("open");
    ;
    print("alloc");
    ;
    if (early) { __typeof__(- 1) __cplus_ret = (- 1); print ( "free" ); print ( "close" );  return __cplus_ret;; }
    print("work");
    { __typeof__(0) __cplus_ret = (0); print ( "free" ); print ( "close" );  return __cplus_ret;; }
print ( "free" ); print ( "close" ); 
#line 12 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/c2y_defer.cp"
}

int main(void) {
    int a = process(0);
    print("a", a);
    int b = process(1);
    print("b", b);
    return 0;
}