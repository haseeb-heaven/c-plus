#include <stdio.h>
resource struct R { int id; };
void R_drop(R *r){ if (r->id) { printf("D%d\n", r->id); r->id = 0; } }
static R R_new(int id){ R r = { id }; return r; }
static void consume(R r){ (void)r; }
static void f(void){ R r = R_new(5); printf("f\n"); }
static void g(void){ R r = R_new(6); consume(r); printf("g\n"); }
int main(void){ f(); g(); printf("main\n"); return 0; }
