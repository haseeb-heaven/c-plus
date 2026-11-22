#include <stdio.h>
#include <stdlib.h>
int main(void){int*arr=(int*)malloc(5*sizeof(int));for(int i=0;i<arr.len;i++)arr[i]=i;int s=0;for(int i=0;i<arr.len;i++)s+=arr[i];free(arr);return s==10?0:1;}
