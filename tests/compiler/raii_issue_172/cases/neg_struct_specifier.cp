#include "cplus/cplus.h"
#include <stdio.h>
resource struct Res { int id; };
void Res_drop(Res *r){ if(r->id){ printf("D%d\n", r->id); r->id=0; } }
int main(void){
  Res owner = {7};
  struct Res *p = &owner;
  Res moved = cplus_move(owner);
  (void)moved;
  printf("%d\n", p->id);
  return 0;
}
