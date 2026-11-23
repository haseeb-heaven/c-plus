#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/core/positive/strcpy_s_valid.c"
/* C+ bounded-copy contract; native strcpy_s may be unavailable on this host. */
#include "cplus/cplus.h"
int main(void){char d[4];return cplus_strcpy(d,sizeof(d),"abc")==3?0:1;}
