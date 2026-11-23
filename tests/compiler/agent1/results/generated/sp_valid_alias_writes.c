#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/core/positive/valid_alias_writes.c"
struct S { int x; };
int main(void) {
  struct S s = {0};
  int *a = &s.x;
  int *b = &s.x;
  *a = 1;
  *b = 2;
  return 0;
}
