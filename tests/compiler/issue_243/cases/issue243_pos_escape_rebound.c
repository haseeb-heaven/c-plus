int *safe(void) {
  int local = 42;
  int *p = 0;
  p = &local;
  p = 0;
  return p;
}

int main(void) {
  int *q = safe();
  return q != 0;
}
