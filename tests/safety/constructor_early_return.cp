class C {
  int x;
  C(void) {
    if (1) return;
    self->x = 1;
  }
};
int main(void) { return 0; }
