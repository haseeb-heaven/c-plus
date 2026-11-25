#include <stdio.h>
#include <stdlib.h>
resource struct Guard { int id; };
void Guard_drop(Guard *self){ if(self->id!=0){ printf("gd=%d\n", self->id); self->id=0; } }
Guard Guard_make(int id){ Guard g; g.id=id; return g; }
int main(int argc, char **argv){
  int idx, v, x;
  int a[4] = {2,4,6,8};
  int *p, *h, *hp;
  (void)argv;
  idx = argc + 1;
  v = a[idx - 1];
  x = v * 3;
  p = a + (idx - 1);
  h = malloc(4*sizeof(int));
  if(!h) return 2;
  h[2] = x;
  hp = h + 2;
  { Guard g = Guard_make(42); (void)g; }
  printf("mix=%d,%d,%d,%d\n", v, x, *p, *hp);
  free(h);
  return 0;
}
