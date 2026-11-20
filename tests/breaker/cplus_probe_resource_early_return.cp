#include <stdio.h>
static int drops = 0;
resource struct Token { int active; };
void Token_drop(Token *token) { if (token->active) drops++; }
int helper(int x) {
  Token a = {1};
  if (x) return 4;
  return 5;
}
int main(void) {
  if (helper(1) != 4 || drops != 1) return 1;
  if (helper(0) != 5 || drops != 2) return 2;
  return 0;
}
