#include "cplus/cplus.h"
int main(void){managed int*p=cplus_new(int);
    managed int*q=cplus_move(p);(void)q;return *p;}
