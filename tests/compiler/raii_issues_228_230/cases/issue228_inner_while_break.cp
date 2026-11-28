#include <stdio.h>
resource struct R { int id; };
void R_drop(R *r){ if (r->id) { printf("D%d\n", r->id); r->id = 0; } }
static R R_new(int id){ R r = { id }; return r; }
int main(void){
  int cond = 1;
  do {
    R outer = R_new(1);
    while (cond) {
      R inner = R_new(2);
      break;
    }
    printf("use %d\n", outer.id);
  } while (0);
  return 0;
}
