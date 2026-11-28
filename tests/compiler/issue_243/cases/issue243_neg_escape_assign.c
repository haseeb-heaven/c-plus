int *bad_escape(void) {
  int local = 42;
  int *p = 0;
  p = &local;
  return p;
}

int main(void) {
  int *q = bad_escape();
  return q ? *q : 0;
}
