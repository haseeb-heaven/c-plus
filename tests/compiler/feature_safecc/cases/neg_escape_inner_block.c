/* issue #180: p is assigned the address of x in an inner block and is still
 * live (returned) after that block ends.  The backward liveness pass on the
 * clang::CFG proves the borrow escapes the frame that owns x. */
int *f(void) {
  int *p = 0;
  {
    int x = 7;
    p = &x;
  }
  return p;
}
int main(void) { return *f(); }
