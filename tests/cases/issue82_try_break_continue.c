#include "cplus/cplus.h"

/* Issue #82: break/continue inside try/catch must target the enclosing user
 * loop or switch, not a compiler-generated do { } while (0) wrapper. */
int main(void) {
  int count = 0;

  for (int i = 0; i < 5; i++) {
    try {
      count += 10;
      break; /* must leave the for loop */
    } catch (Exception ex) {
      (void)ex;
    }
  }
  print("for_break", count);

  count = 0;
  for (int i = 0; i < 3; i++) {
    try {
      throw "boom";
    } catch (Exception ex) {
      (void)ex;
      continue; /* must skip the rest of the loop body */
    }
    count += 100;
  }
  print("catch_continue", count);

  count = 0;
  while (count < 5) {
    try {
      throw "boom";
    } catch (Exception ex) {
      (void)ex;
      break; /* must leave the while loop */
    }
  }
  print("while_break", count);

  count = 0;
  switch (1) {
  case 1:
    try {
      count = 7;
      break; /* must leave the switch, not the try */
    } catch (Exception ex) {
      (void)ex;
    }
    count = 99; /* must be skipped */
    break;
  default:
    count = 88;
    break;
  }
  print("switch_break", count);
  return 0;
}
