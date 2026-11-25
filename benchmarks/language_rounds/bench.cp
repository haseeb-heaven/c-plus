/* Same workload as bench.rs: 2M iterations of integer mix + bounded updates. */
#include <stdio.h>
#include <time.h>
#include "cplus/cplus.h"
static long long now_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);
  return (long long)t.tv_sec*1000000000LL + t.tv_nsec; }
int main(void) {
  const long N = 2000000; long i; unsigned long long acc = 0; long long t0, t1;
  struct P { long x, y; } p = {1, 2};
  t0 = now_ns();
  for (i = 0; i < N; ++i) {
    p.x = (p.x * 3 + p.y) & 0xffffffL;
    p.y = (p.y + i * 7) & 0xffffffL;
    acc += (unsigned long long)(p.x ^ p.y);
  }
  t1 = now_ns();
  printf("CPLUS %lld %llu\n", t1 - t0, acc);
  return acc == 0 ? 1 : 0;
}
