struct S { int x; };
int main(void) {
  struct S s = {0};
  int *a = &s.x;
  int *b = &s.x;
  *a = 1;
  *b = 2;
  return s.x;
}
