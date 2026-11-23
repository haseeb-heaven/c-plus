#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/managed_len_expr.cp"
#include "cplus/cplus.h"
int main(void) {
   int *xs = cplus_new_array(int, 7); 
  if (((7)) != 7) { __typeof__(1) __cplus_ret = (1); cplus_release(xs); xs = NULL;  return __cplus_ret;; }
  for (__typeof__( 0) i = 0; i < ((7)); i++) xs[i] = i + 1;
  { __typeof__(xs [ 6 ] == 7 ? 0 : 2) __cplus_ret = (xs [ 6 ] == 7 ? 0 : 2); cplus_release(xs); xs = NULL;  return __cplus_ret;; }
cplus_release(xs); xs = NULL; 
#line 8 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/managed_len_expr.cp"
}
