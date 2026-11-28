/* Fully working small line editor (plain C89).
 * stdin commands: a <text>\n append, p print, d delete last, q quit. */
#include <stdio.h>
#include <string.h>

#define LINES 20
#define WIDTH 80
static char buf[LINES][WIDTH];
static int nlines;

int main(void) {
  char line[WIDTH];
  while (fgets(line, WIDTH, stdin)) {
    size_t n = strlen(line);
    if (n > 0 && line[n - 1] == '\n')
      line[n - 1] = '\0';
    if (line[0] == 'q')
      break;
    if (line[0] == 'p') {
      int i;
      for (i = 0; i < nlines; i++)
        printf("%d: %s\n", i + 1, buf[i]);
    } else if (line[0] == 'd') {
      if (nlines > 0)
        nlines--;
    } else if (line[0] == 'a') {
      if (nlines < LINES) {
        size_t k;
        for (k = 2; k < WIDTH && line[k]; k++)
          buf[nlines][k - 2] = line[k];
        buf[nlines][WIDTH - 1] = '\0';
        nlines++;
      }
    }
  }
  printf("lines=%d\n", nlines);
  return 0;
}
