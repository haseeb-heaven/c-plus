#include "cplus/cplus.h"
#include <stdio.h>
resource struct R { int id; };
void R_drop(R *r){ if(r->id){ printf("D%d ", r->id); r->id=0; } }
int main(void){ dtor R a={1}; { dtor R b={2}; } printf("END\n"); return 0; }
