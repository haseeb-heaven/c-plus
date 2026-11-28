#include "cplus/cplus.h"
int main(void){ int *p=cplus_new(int); *p=7; cplus_release(p); return *p; }
