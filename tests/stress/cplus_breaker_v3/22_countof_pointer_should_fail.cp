#include "cplus/cplus.h"
int wrong(int *p){ return countof(p); }
int main(void){int a[4]={0};return wrong(a);}
