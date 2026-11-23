#include <stddef.h>
extern void *cplus_register_allocation(void *, size_t, const char *, int);
extern void cplus_free_registered(void *, const char *, int);
extern void *cplus_realloc_registered(void *, void *, size_t, const char *, int);
#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/core/positive/len_malloc_constant.c"
#include <stdio.h>
#include <stdlib.h>
int main(void){int*arr=(int*)cplus_register_allocation((malloc(5*sizeof(int))), 20, __FILE__, __LINE__);for(int i=0;i<((5 * sizeof ( int )) / sizeof(*arr));i++)arr[i]=i;int s=0;for(int i=0;i<((5 * sizeof ( int )) / sizeof(*arr));i++)s+=arr[i];cplus_free_registered(arr, __FILE__, __LINE__);return s==10?0:1;}
