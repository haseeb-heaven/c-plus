/* A loop counter starting at zero plus a zero offset can still be zero. */
int f(void) {
  int r = 0;
  for (int i = 0; i < 3; i++) r += 10 / (i + 0);
  return r;
}
int main(void) { return f(); }
