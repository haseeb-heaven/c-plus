/* A genuine `if (!p) return;` guard stays accepted. */
int f(int *p) {
  if (!p) return 0;
  *p = 1;
  return *p;
}
int main(void) { int x = 0; return f(&x); }
