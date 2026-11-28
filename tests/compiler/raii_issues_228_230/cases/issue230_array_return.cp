#include <stdio.h>
resource struct R { int id; };
void R_drop(R *r){ if (r->id) { printf("D%d\n", r->id); r->id = 0; } }
static R R_new(int id){ R r = { id }; return r; }
static int buf[4] = {1,2,3,4};
static int *get(void){ R r = R_new(8); return buf; }
int main(void){ printf("%d\n", get()[2]); return 0; }
