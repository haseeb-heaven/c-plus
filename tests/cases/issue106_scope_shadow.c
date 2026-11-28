/* Agent 1 issue #106: class receiver lookup must respect lexical scopes and
 * plain-C declarations, not declaration order alone. */
#include "cplus/cplus.h"

class Alpha {
  int v;
  int get(void) { return 11; }
};

class Beta {
  int v;
  int get(void) { return 22; }
};

static int plain_get(void) { return 33; }

void first_function(void) {
  Alpha s = {0};
  print("first", s.get());
}

void second_function(void) {
  struct Plain { int (*get)(void); } s;
  s.get = plain_get;
  print("plain", s.get()); /* must stay a plain C function-pointer call */
}

int main(void) {
  Alpha x = {0};
  {
    Beta x = {0};
    print("inner", x.get());
  }
  print("outer", x.get()); /* outer Alpha binding is alive again */
  first_function();
  second_function();
  return 0;
}
