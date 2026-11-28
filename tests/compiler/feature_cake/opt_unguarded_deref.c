/* Cake _Opt: a dereference with no active non-null refinement must be rejected. */
int main(void) {
  int value = 7;
  int *_Opt p = &value;
  *p = 9;
  return *p;
}
