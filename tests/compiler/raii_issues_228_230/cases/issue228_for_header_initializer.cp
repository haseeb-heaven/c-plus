#include <stdio.h>
resource struct R { int id; };
void R_drop(R *r){ if (r->id) { printf("D%d\n", r->id); r->id = 0; } }
static R R_new(int id){ R r = { id }; return r; }
struct S { int x; };
int main(void){
  for (struct S s = {1}; s.x < 4; ++s.x) {
    R r = R_new(s.x);
    printf("I%d\n", s.x);
    if (s.x == 2) break;
  }
  printf("done\n");
  return 0;
}
