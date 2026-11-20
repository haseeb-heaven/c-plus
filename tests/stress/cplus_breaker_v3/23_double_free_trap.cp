#include "cplus/cplus.h"
int main(void){int *p=cplus_new_array(int,8); p[0]=1; cplus_release(p); cplus_release(p); return 0;}
