#include <stddef.h>
static int visit(int v, int (*f)(int)){ return f(v); }
static int twice(int v){ return 2*v; }
int main(void){ return visit(21,twice)==42 ? 0 : 1; }
