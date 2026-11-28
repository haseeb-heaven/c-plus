#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/runtime_fail/alloc_overflow.c"
#include <stdint.h>
#include "cplus/cplus.h"
int main(void) {
     int *p = cplus_new_array(int, SIZE_MAX / 2); 
    (void)p;
    { __typeof__(0) __cplus_ret = (0); cplus_release(p); p = NULL;  return __cplus_ret;; }
cplus_release(p); p = NULL; 
#line 8 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/runtime_fail/alloc_overflow.c"
}
