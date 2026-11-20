/* Bug 46: C23 auto drops const; C+ __typeof__ keeps it (safer). */
#include "cplus/cplus.h"
int main(void) { const int x = 42; auto y = x; y = 99; print(y); return 0; }
