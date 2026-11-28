/* Original deterministic left-to-right expression evaluator (plain C89).
 * Accumulators avoid negative subscripts entirely. */
#include <stdio.h>
#include <string.h>

static int value(char c) {
  if (c >= '0' && c <= '9')
    return (int)(c - '0');
  return 0;
}

int main(void) {
  const char *expr = "1+2*3";
  int a = 0, b = 0, pending = 0, have = 0;
  int i;
  for (i = 0; i < (int)strlen(expr); i++) {
    char c = expr[i];
    if (c >= '0' && c <= '9') {
      int v = value(c);
      if (!have) {
        a = v;
        have = 1;
      } else {
        b = v;
        if (pending == '+')
          a = a + b;
        else if (pending == '*')
          a = a * b;
        pending = 0;
      }
    } else if (c == '+' || c == '*') {
      pending = (int)c;
    }
  }
  printf("result=%d\n", a);
  return 0;
}
