#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/core/positive/len_malloc_constant.c"
#include <stdio.h>
#include <stdlib.h>
int main(void){int*arr=(int*)malloc(5*sizeof(int));for(int i=0;i<((5 * sizeof ( int )) / sizeof(*arr));i++)arr[i]=i;int s=0;for(int i=0;i<((5 * sizeof ( int )) / sizeof(*arr));i++)s+=arr[i];free(arr);return s==10?0:1;}