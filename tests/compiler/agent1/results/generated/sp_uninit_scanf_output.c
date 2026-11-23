#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/core/positive/uninit_scanf_output.c"
#include <stdio.h>
int main(void) {
  int x = 0;
  scanf("%d", &x);
  return x == 0 ? 0 : 1;
}
