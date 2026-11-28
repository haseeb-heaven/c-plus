#include "cplus/cplus.h"
int main(void) { int *p = cplus_new(int); cplus_release(p); cplus_release(p); return 0; }
