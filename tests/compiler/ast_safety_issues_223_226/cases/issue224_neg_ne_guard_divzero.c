/* issue #224: `x != 0` means x IS zero on the continuing path. */
int f(int x) {
  x = 0;
  if (x != 0) return 0;
  return 1 / x;
}
