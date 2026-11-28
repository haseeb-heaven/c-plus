#include <stdio.h>
resource struct R { int id; };
void R_drop(R *r){ if (r->id) { printf("D%d\n", r->id); r->id = 0; } }
static R R_new(int id){ R r = { id }; return r; }
static void consume(R r){ printf("C%d\n", r.id); }
int main(void){ R r = R_new(9); if (0) consume(r); printf("skip\n"); return 0; }
