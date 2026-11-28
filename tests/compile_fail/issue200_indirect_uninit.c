/* Issue #200: an indirect call through a function pointer must not mark its
 * arguments initialized. The callee body here only reads its argument. */
static void read_only(int *p) { volatile int v = *p; (void)v; }

int main(void) {
  void (*cb)(int *) = read_only;
  int uninit;
  cb(&uninit);
  return uninit;
}
