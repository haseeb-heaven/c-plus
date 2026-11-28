/* Issue #228: do/while state is per loop frame.  A nested `while` must push
 * its own frame, otherwise `break` inside it drops owners from the enclosing
 * `if` / `do` bodies (double free) and leaves the real body uncleaned. */
#include "cplus/cplus.h"
#include <stdio.h>

resource struct Box { int id; };

void Box_drop(Box *b) {
  if (b->id) {
    printf("drop %d\n", b->id);
    b->id = 0;
  }
}

struct Pair { int x, y; };

static void nested_do_while(void) {
  int n = 1;
  do {
    Box outerR = {10};
    if (n) {
      Box midR = {20};
      while (n) {
        Box innerWhileR = {30};
        break;
      }
      printf("mid %d\n", midR.id);
    }
    printf("outer %d\n", outerR.id);
  } while (0);
}

/* A `{` inside a parenthesised condition is a compound literal, not the loop
 * body: it must neither bind bodyPos nor retire the loop frame. */
static void paren_guard(void) {
  int n = 1;
  while (((struct Pair){n, 2}).x) {
    Box bodyR = {40};
    if (n) {
      Box innerParenR = {50};
      break;
    }
  }
}

int main(void) {
  nested_do_while();
  paren_guard();
  return 0;
}
