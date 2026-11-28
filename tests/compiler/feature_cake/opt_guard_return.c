/* Cake _Opt: `if (!p) return;` refines p for all following statements. */
int main(void) {
  int value = 7;
  int *_Opt p = &value;
  if (!p) return 1;
  *p = 9;
  return *p;
}
