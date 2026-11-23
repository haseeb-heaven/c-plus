#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/memory.hp"

static unsigned long long g=0x9E3779B97F4A7C15ULL;
static unsigned long long nxt(void){
  unsigned long long z;
  g += 0x9E3779B97F4A7C15ULL; z=g;
  z=(z^(z>>30))*0xBF58476D1CE4E5B9ULL;
  z=(z^(z>>27))*0x94D049BB133111EBULL;
  return z^(z>>31);
}
static int all_a5(const void *p){ const unsigned char*q=(const unsigned char*)p; for(int i=0;i<64;i++) if(q[i]!=0xA5u) return 0; return 1;}
static void mkstale(void **so, unsigned long long *to){
  void *p=stdlib_alloc(64); *to=stdlib_mem_token(p); *so=p; stdlib_release(p);
}
int main(void){
  for(int i=0;i<15;i++){nxt();nxt();nxt();}
  unsigned long long a=nxt(),b=nxt(),c=nxt();
  (void)b;(void)c;
  printf("case15 a=%llu variant=%llu\n",a,a%8);
  void *dst=stdlib_alloc(64); void *src=stdlib_alloc(64);
  unsigned long long gen=stdlib_mem_token(dst);
  void *stale=(void*)0; unsigned long long stale_tok=0;
  mkstale(&stale,&stale_tok);
  memset(dst,0xA5u,64u); memset(src,0x5Au,64u);
  int variant=(int)(a%8);
  int rc=-99;
  if(variant==5) rc=stdlib_mem_move(dst,SIZE_MAX,src,0u,8u,gen,(error*)0);
  printf("rc=%d all_a5=%d gen=%llu stale_tok=%llu\n",rc,all_a5(dst),gen,stale_tok);
  const unsigned char*q=(const unsigned char*)dst;
  for(int i=0;i<16;i++) printf("%02X ",*(q+i));
  printf("\n");
  return 0;
}
