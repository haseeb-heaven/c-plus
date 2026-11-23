#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/core/positive/multi_tu_resource_main.c"
#include "multi_tu_resource.h"
int main(void) {
  SharedR r = SharedR_make(21);
  return SharedR_value(&r) == 21 ? 0 : 1;
}