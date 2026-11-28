typedef int (*Deref)(int *);
static int deref(int *p) { return *p; }
int main(void) {
  volatile Deref fn = deref;
  return fn(0);
}
