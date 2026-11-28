/* Cake _Opt: a true-branch refinement permits the dereference inside it. */
int main(void) {
  int value = 7;
  int *_Opt p = &value;
  if (p) {
    *p = 9;
    return *p;
  }
  return 0;
}
