#include "cplus/cplus.h"
#include <stdio.h>
generic Vec<T>;
int main(void){
  int a[3] = {10,20,30};
  Vec<int> vi; Vec_int_init(&vi);
  vi.data = a; vi.len = 3; vi.cap = 3;
  double d[2] = {1.5,2.5};
  Vec<double> vd; Vec_double_init(&vd);
  vd.data = d; vd.len = 2; vd.cap = 2;
  printf("I=%d L=%zu SZ=%d D=%.1f\n", Vec_int_at(&vi,1),
         Vec_int_len(&vi),
         sizeof(Vec<int>)==sizeof(int*)+2*sizeof(size_t),
         Vec_double_at(&vd,0));
  return 0;
}
