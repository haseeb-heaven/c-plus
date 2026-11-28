#include <stdio.h>

/* Real-world style: link against a separately compiled static library whose
   code was built by the same toolchain under test. */
struct LibPair { long a; double b; };
extern int lib_mul(int, int);
extern double lib_scale(double);
extern struct LibPair lib_pair(long, double);
extern int lib_apply(int (*fn)(int, int), int, int);
static int cb(int a, int b) { int t = a + b; return t; }
int main(void) {
  struct LibPair p;
  p = lib_pair(6, 7);
  printf("lib=%d,%.1f,%ld,%.1f,cb=%d\n",
         lib_mul(6, 7), lib_scale(1.25), p.a, p.b, lib_apply(cb, 4, 5));
  return 0;
}
