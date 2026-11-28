#include "cplus/cplus.h"
int main(void) { int *p = cplus_new(int); cplus_release(p); cplus_check_live(p); return 0; }
