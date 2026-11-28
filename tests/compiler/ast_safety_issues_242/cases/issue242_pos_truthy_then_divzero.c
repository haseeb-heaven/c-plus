/* Inside the then arm of `if (x)`, x is provably non-zero. */
int f(int x) {
  if (x) return 10 / x;
  return 0;
}
int main(void) { return f(1); }
