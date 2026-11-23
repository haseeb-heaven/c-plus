#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/compile_fail/classic_bug46_auto_const.cp"
/* Bug 46: C23 auto drops const; C+ __typeof__ keeps it (safer). */
#include "cplus/cplus.h"
int main(void) { const int x = 42; __typeof__( x) y = x; y = 99; print(y); return 0; }