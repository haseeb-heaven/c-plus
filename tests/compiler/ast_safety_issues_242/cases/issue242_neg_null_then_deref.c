/* `if (!p)` enters the then arm on the null path; that dereference
 * must be diagnosed by the AST safety pass as well. */
int f(void) {
  int *p = 0;
  if (!p) { *p = 1; }
  return *p;
}
int main(void) { return f(); }
