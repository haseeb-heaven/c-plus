#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* Real-world style: dispatch table entries compared by address and by raw
   representation, then stored in and returned from a holder struct. */
typedef int (*Fn)(int, int);
struct Holder { Fn f; long tag; };
static int rw_target(int a, int b) { int t = a * 10; int u = t + b; return u; }
static int rw_other(int a, int b) { int t = a - b; return t; }
static Fn rw_roundtrip(Fn f) { struct Holder h; h.f = f; h.tag = 9; return h.f; }
int main(void) {
  Fn a = rw_target, b = &rw_target, c = rw_other;
  uintptr_t ua = 0, ub = 0, uc = 0;
  memcpy(&ua, &a, sizeof a); memcpy(&ub, &b, sizeof b);
  memcpy(&uc, &c, sizeof c);
  printf("sz=%zu,%zu ua=%zu eq=%d same=%d diff=%d call=%d rt=%d\n",
         sizeof(Fn), sizeof(void *), sizeof(uintptr_t), a == b ? 1 : 0,
         ua == ub ? 1 : 0, ua != uc ? 1 : 0, a(4, 2), rw_roundtrip(a)(5, 3));
  return 0;
}
