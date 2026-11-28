#include <stdio.h>
resource struct R { int id; };
void R_drop(R *r){ if (r->id) { printf("D%d\n", r->id); r->id = 0; } }
static R R_new(int id){ R r = { id }; return r; }
int take(void){ R a = R_new(4); return (R_drop(&a), 7); }
int main(void){ printf("%d\n", take()); return 0; }
