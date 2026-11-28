#include <stdio.h>
#include "cplus/cplus.h"
static void *g_ptrs[32]; static int g_n = 0;
resource struct R { int id; int *p; };
void R_drop(R *r){ if (r->id) { printf("D%d\n", r->id); cplus_release(r->p); r->id = 0; } }
static R R_new(int id){ R r = { id, (int *)cplus_alloc(4) }; g_ptrs[g_n++] = r.p; return r; }

static int scenario(void){
  R a = R_new(1);
  if (a.id) {
    R b = R_new(2);
    if (b.id) {
      R c = R_new(3);
      printf("A\n");
      return 7;
    } else {
      R d = R_new(4);
      printf("B\n");
    }
    printf("C\n");
  } else {
    R e = R_new(5);
    printf("D\n");
  }
  printf("E\n");
  return 0;
}

int main(void){
  int rc = scenario();
  int live = 0;
  for (int i = 0; i < g_n; ++i) {
    if (cplus_allocation_generation(g_ptrs[i]) != 0) ++live;
  }
  printf("rc=%d live=%d\n", rc, live);
  return 0;
}
