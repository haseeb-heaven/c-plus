#include "cplus/cplus.h"
#include <stdio.h>
resource struct Res { int id; };
void Res_drop(Res *r){ if(r->id){ printf("D%d\n", r->id); r->id=0; } }
int main(void){
  Res owner = {7};
  const Res *p = &owner;
  printf("P%d\n", p->id);
  return 0;
}
