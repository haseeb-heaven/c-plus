struct Pair { long x; double y; };
int ext_add(int a, int b) { int t = a + b; return t; }
long ext_long(long a, long b, long c) { long t = a + b; long u = t + c; return u; }
double ext_double(double a, double b) { double t = a * b; return t; }
int ext_ptr(const int *p) { return *p; }
struct Pair ext_pair(long x, double y) { struct Pair p; p.x = x; p.y = y; return p; }
