#include <stdio.h>
#define SWAP(a,b) do { int t=(a); (a)=(b); (b)=t; } while(0)
int main(void) { int a=4,b=5; SWAP(a,b); return a==5 && b==4 ? 0:1; }
