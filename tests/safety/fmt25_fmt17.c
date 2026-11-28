#include <stdio.h>
int main(void){int r[4];size_t n=fread(r,sizeof(r),1,stdin);if(n==0)return r[0];return 0;}
