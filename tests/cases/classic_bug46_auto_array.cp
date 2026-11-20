/* Bug 46: arrays decay with auto in C+ (fixed by pointer-type inference). */
#include "cplus/cplus.h"
int main(void) { int arr[4] = {1,2,3,4}; auto p = arr; print(p[0], p[3]); return 0; }
