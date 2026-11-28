/* Issue #235 control TU: no findings, so only the injected pass-through
 * flags can change the STRICT check's outcome. */
int main(void) {
  int x = 7;
  return x == 7 ? 0 : 1;
}
