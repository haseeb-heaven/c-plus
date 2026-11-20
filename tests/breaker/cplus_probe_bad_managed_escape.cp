#include "cplus/cplus.h"
int *bad(void) { managed int *p = cplus_new(int); return p; }
int main(void) { return 0; }
