#include <stdio.h>
#include <stdint.h>
#include <string.h>

typedef int (*F2)(int,int);
typedef long (*F1)(long);
struct Holder { F2 f; long tag; };
static int target(int a,int b){ int t=a*10; int u=t+b; return u; }
static int other(int a,int b){ int t=a-b; return t; }
static F2 roundtrip(F2 f){ struct Holder h; h.f=f; h.tag=9; return h.f; }
int main(void){
  F2 a=target; F2 b=&target; F2 c=other; F1 g=(F1)target;
  uintptr_t ua=0; uintptr_t ub=0; uintptr_t uc=0; uintptr_t ug=0;
  memcpy(&ua,&a,sizeof a); memcpy(&ub,&b,sizeof b);
  memcpy(&uc,&c,sizeof c); memcpy(&ug,&g,sizeof g);
  printf("sz=%zu ptr=%zu ua=%zu eq=%d same=%d diff=%d cross=%d call=%d rt=%d\n",
         sizeof(F2), sizeof(void*), sizeof(uintptr_t), a==b?1:0,
         ua==ub?1:0, ua!=uc?1:0, ua==ug?1:0, a(4,2), roundtrip(a)(5,3));
  return 0;
}
