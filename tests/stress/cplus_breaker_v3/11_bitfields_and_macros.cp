#include <stdio.h>
#define EXCHANGE(a,b) do { int t=(a); (a)=(b); (b)=t; } while(0)
struct Flags {unsigned active:1; unsigned mode:3;};
int main(void){ struct Flags f={.active=1,.mode=5}; int x=2,y=9; if(f.active)EXCHANGE(x,y); printf("BITS %u %u %d %d\n",f.active,f.mode,x,y);return 0; }
