#include <stddef.h>
extern void *cplus_register_allocation(void *, size_t, const char *, int);
extern void cplus_free_registered(void *, const char *, int);
extern void *cplus_realloc_registered(void *, void *, size_t, const char *, int);
#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/core/positive/malloc_valid.c"
#include <stdlib.h>
int main(void){int*p=cplus_register_allocation((malloc(4*sizeof(int))), 16, __FILE__, __LINE__);if(!p)return 1;p[0]=7;int v=p[0];cplus_free_registered(p, __FILE__, __LINE__);return v==7?0:1;}
