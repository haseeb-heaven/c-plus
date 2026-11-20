#include "cplus/cplus.h"
int *leak(void){ managed int *p=cplus_new_array(int,3); return p; }
int main(void){return leak()!=0;}
