#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/core/positive/realloc_success.c"
#include "cplus/cplus.h"
int main(void){int*p=cplus_realloc(NULL,sizeof(int));if(!p)return 1;*p=9;int v=*p;cplus_release(p);return v==9?0:1;}