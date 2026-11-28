#include <stddef.h>
#include <stdio.h>
int main(void){char b[64];size_t h=(size_t)-1/4+10;size_t n=fread(b,4,h,stdin);return (int)n;}
