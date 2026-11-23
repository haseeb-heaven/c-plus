#include <stddef.h>
extern void *cplus_register_allocation(void *, size_t, const char *, int);
extern void cplus_free_registered(void *, const char *, int);
extern void *cplus_realloc_registered(void *, void *, size_t, const char *, int);
#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/core/positive/branch_free_null_ok.c"
#include <stdlib.h>
int main(int argc,char**argv){ int *p=cplus_register_allocation((malloc(16)), 16, __FILE__, __LINE__); if(!p)return 1; if(argc>1){ cplus_free_registered(p, __FILE__, __LINE__); p=0; } cplus_free_registered(p, __FILE__, __LINE__); return 0; }