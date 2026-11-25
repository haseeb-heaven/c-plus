#include "multi_tu_resource.h"
int main(void) {
  SharedR r = SharedR_make(21);
  return SharedR_value(&r) == 21 ? 0 : 1;
}
