/* `if (x == 0) return;` really proves x non-zero afterwards. */
int f(int x) {
  if (x == 0) return 0;
  return 10 / x;
}
int main(void) { return f(1); }
