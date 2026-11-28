/* When only the false arm returns, the continuing path still has x != 0. */
int f(int x) {
  if (x) { return 10 / x; } else { return 1; }
}
int main(void) { return f(1); }
