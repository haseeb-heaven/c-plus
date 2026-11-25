struct S { int x; };
int main(void) {
  struct S s = {0};
  int *a = &s.x;
  *a = 1;
  return s.x == 1 ? 0 : 1;
}
