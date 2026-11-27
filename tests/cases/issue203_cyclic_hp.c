/* A guarded a.hp <-> b.hp cycle used to overflow the driver's recursion. */
#include "issue203_a.hp"
#include <stdio.h>
int main(void) {
  Issue203A a = {3};
  Issue203B b = {4};
  printf("%d\n", a.get() + b.get());
  return 0;
}
