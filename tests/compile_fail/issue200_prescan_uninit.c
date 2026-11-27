/* Issue #200: a name merely containing "scan" must not be treated as an
 * output function. prescan only reads through a const pointer, so the
 * uninitialized local passed by address is still read uninitialized. */
void prescan(const int *p) { volatile int v = *p; (void)v; }

int main(void) {
  int uninit;
  prescan(&uninit);
  return uninit;
}
