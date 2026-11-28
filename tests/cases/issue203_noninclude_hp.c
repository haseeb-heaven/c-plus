/* #include "issue203_missing_comment.hp" */
// #include "issue203_missing_line.hp"
const char *msg = "#include \"issue203_missing_string.hp\"";
#if 0
#include "issue203_missing_if0.hp"
this branch is disabled and must not be rewritten
#endif
#if 1
#include "issue203_live.hp"
#endif
#include <stdio.h>
int main(void) {
  Issue203Live v = {5};
  printf("%d%s\n", v.get(), msg);
  return 0;
}
