/* Cake _Opt: short-circuit RHS is reachable only when p is non-null. */
int main(void) {
  int value = 0;
  int *_Opt p = &value;
  if (p && *p == 0) return 1;
  return 0;
}
