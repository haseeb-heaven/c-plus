int global_value = 7;
struct Box { int *p; };

struct Box safe_box(void) {
  struct Box s;
  s.p = &global_value;
  return s;
}

int main(void) {
  struct Box b = safe_box();
  return *b.p;
}
