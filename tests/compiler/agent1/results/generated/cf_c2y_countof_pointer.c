#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/compile_fail/c2y_countof_pointer.c"
#include "cplus/cplus.h"
void bad(int arr[]) { size_t n = countof(arr); (void)n; }
int main(void) { return 0; }
