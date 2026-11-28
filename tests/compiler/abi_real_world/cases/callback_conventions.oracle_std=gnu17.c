#include <stdio.h>

/* Real-world style: callbacks with int, double and struct-returning
   signatures, invoked through parameters and an operations table. */
struct Pair { long x; double y; };
static int cb_add(int a, int b) { int t = a + b; return t; }
static double cb_mul(double a, double b) { double t = a * b; return t; }
static struct Pair cb_pair(long x, double y) {
  struct Pair p; p.x = x; p.y = y; return p;
}
static int invoke2(int (*f)(int, int), int a, int b) { int t = f(a, b); return t; }
static double invoke_dbl(double (*f)(double, double), double a, double b) {
  double t = f(a, b); return t;
}
static struct Pair invoke_pair(struct Pair (*f)(long, double), long a, double b) {
  struct Pair p = f(a, b); return p;
}
struct Ops { int (*add)(int, int); double (*mul)(double, double); };
int main(void) {
  struct Pair p; struct Ops ops;
  p = invoke_pair(cb_pair, 7, 2.5);
  ops.add = cb_add; ops.mul = cb_mul;
  printf("cb=%d,%.1f,%ld,%.1f,struct=%d,%.1f\n",
         invoke2(cb_add, 20, 22), invoke_dbl(cb_mul, 1.5, 4.0),
         p.x, p.y, ops.add(3, 4), ops.mul(2.0, 3.0));
  return 0;
}
