#include <stdio.h>

/* Real-world style: a consumer TU with extern declarations whose definitions
   live in another translation unit (int/long/double/pointer/struct returns). */
struct Pair { long x; double y; };
extern int ext_add(int, int);
extern long ext_long(long, long, long);
extern double ext_double(double, double);
extern int ext_ptr(const int *);
extern struct Pair ext_pair(long, double);
static int hidden_add(int a, int b) { int t = a - b; return t; }
int main(void) {
  struct Pair p; int v = 5;
  p = ext_pair(7, 2.5);
  printf("x=%d,%ld,%.1f,%d,%ld,%.1f,h=%d\n",
         ext_add(20, 22), ext_long(1, 2, 3), ext_double(1.5, 4.0),
         ext_ptr(&v), p.x, p.y, hidden_add(9, 2));
  return 0;
}
