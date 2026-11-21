#include "cplus/cplus.h"
#define N 1100
int main(void){
  int *stale = cplus_new(int);
  cplus_release(stale);
  for (int i=0;i<N;i++){ int *q=cplus_alloc(sizeof(int)); cplus_release(q); }
  int *fresh = cplus_alloc(sizeof(int));
  cplus_check_live(stale);
  (void)fresh;
  return 0;
}
