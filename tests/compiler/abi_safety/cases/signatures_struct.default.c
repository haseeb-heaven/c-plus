#include <stdio.h>

struct Pair { long x; double y; };
struct Big { long a,b,c,d; };
static struct Pair mk_pair(long x,double y){ struct Pair p; p.x=x; p.y=y; return p; }
static struct Big mk_big(long a,long b,long c,long d){
  struct Big s; s.a=a; s.b=b; s.c=c; s.d=d; return s;
}
static long sum_big(struct Big s){ long t=s.a+s.b; long u=s.c+s.d; long v=t+u; return v; }
int main(void){
  struct Pair p=mk_pair(7,2.5);
  struct Big g=mk_big(11,22,33,44);
  printf("p=%ld,%.1f big=%ld,%ld,%ld,%ld sz=%zu sum=%ld\n",
         p.x, p.y, g.a, g.b, g.c, g.d, sizeof(struct Big), sum_big(g));
  return 0;
}
