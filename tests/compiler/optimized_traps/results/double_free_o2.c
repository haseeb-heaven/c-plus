#include "cplus/cplus.h"
int main(void){int*p=cplus_alloc(4);cplus_release(p);cplus_release(p);return 0;}
