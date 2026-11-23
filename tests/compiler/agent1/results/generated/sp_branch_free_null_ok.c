#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/core/positive/branch_free_null_ok.c"
#include <stdlib.h>
int main(int argc,char**argv){ int *p=malloc(16); if(!p)return 1; if(argc>1){ free(p); p=0; } free(p); return 0; }