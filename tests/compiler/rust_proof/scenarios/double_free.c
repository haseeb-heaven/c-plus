#include "cplus/cplus.h"
int main(void){managed int *p=cplus_new(int);*p=1;
               cplus_release(p);cplus_release(p);return 0;}
