#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/core/positive/managed_len_fold.c"
#include "cplus/cplus.h"
int main(void) {
   int *p = cplus_new_array(int, 5); 
  if (((5)) != 5) { __typeof__(1) __cplus_ret = (1); cplus_release(p); p = NULL;  return __cplus_ret;; }
  for (size_t i = 0; i < ((5)); i++) p[i] = (int)i;
  { __typeof__(p [ 4 ] == 4 ? 0 : 1) __cplus_ret = (p [ 4 ] == 4 ? 0 : 1); cplus_release(p); p = NULL;  return __cplus_ret;; }
cplus_release(p); p = NULL; 
#line 8 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/core/positive/managed_len_fold.c"
}
