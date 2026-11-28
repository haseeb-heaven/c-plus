#include <stdio.h>

/* Real-world style: a module exporting functions and globals while keeping
   its helpers static. */
int rw_glob_counter = 41;
const char *rw_glob_name = "rw";
int rw_add_exported(int a, int b) { int t = a + b; return t; }
static int rw_add_hidden(int a, int b) { int t = a - b; return t; }
int main(void) {
  rw_glob_counter = 42;
  printf("sym=%d,%d,%s,%d\n", rw_add_exported(3, 4), rw_add_hidden(9, 2),
         rw_glob_name, rw_glob_counter);
  return 0;
}
