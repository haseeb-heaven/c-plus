struct Box { int *p; };

struct Box bad_box(void) {
  int local = 42;
  struct Box s;
  s.p = &local;
  return s;
}

int main(void) {
  struct Box b = bad_box();
  return *b.p;
}
