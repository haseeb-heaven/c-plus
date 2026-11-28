struct LibPair { long a; double b; };
int lib_mul(int a, int b) { int t = a * b; return t; }
double lib_scale(double x) { double t = x * 2.0; return t; }
struct LibPair lib_pair(long a, double b) {
  struct LibPair p; p.a = a; p.b = b; return p;
}
int lib_apply(int (*fn)(int, int), int a, int b) { int t = fn(a, b); return t; }
