#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/classic_bug46_auto_array.cp"
/* Bug 46: arrays decay with auto in C+ (fixed by pointer-type inference). */
#include "cplus/cplus.h"
int main(void) { int arr[4] = {1,2,3,4}; __typeof__(&arr[0]) p = arr; print(p[0], p[3]); return 0; }