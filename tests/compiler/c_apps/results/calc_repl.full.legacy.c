#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/compiler/c_apps/full_apps/calc_repl.c"
/* Fully working interactive calculator REPL (plain C89).
 * stdin: one expression per line using + and * and digits; q quits. */
#include <stdio.h>
#include <string.h>

static int value(char c) {
  if (c >= '0' && c <= '9') return (int)(c - '0');
  return 0;
}

int main(void) {
  char line[128];
  while (fgets(line, 128, stdin)) {
    int a = 0, b = 0, pending = 0, have = 0, i;
    size_t n = strlen(line);
    if (n > 0 && line[n - 1] == '\n') line[n - 1] = '\0';
    if (line[0] == 'q') break;
    for (i = 0; line[i]; i++) {
      char c = line[i];
      if (c >= '0' && c <= '9') {
        int v = value(c);
        if (!have) { a = v; have = 1; }
        else {
          b = v;
          if (pending == '+') a = a + b;
          else if (pending == '*') a = a * b;
          pending = 0;
        }
      } else if (c == '+' || c == '*') {
        pending = (int)c;
      }
    }
    if (have) printf("= %d\n", a);
  }
  printf("bye\n");
  return 0;
}
