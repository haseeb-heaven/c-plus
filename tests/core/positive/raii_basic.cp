#include "cplus/cplus.h"
static int seq = 0;
resource struct Token { int id; };
void Token_drop(Token *t) { if (t->id) { seq = seq * 10 + t->id; t->id = 0; } }
static int helper(int early) {
  Token a = {1};
  Token b = {2};
  if (early) return 42;
  return 7;
}
int main(void) { return helper(1) == 42 && seq == 21 ? 0 : 1; }
