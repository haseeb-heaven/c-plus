/* `if (x) return;` exits on the non-zero path, so x is zero on the
 * fall-through path. The old checker kept the non-zero proof and hid this. */
int div_zero(int x) {
  if (x) return 0;
  return 10 / x;
}
int main(void) { return div_zero(0); }
