/* issue #180: a mutex acquired only on one arm is still held on that exit
 * path.  The linear walk restores heldLocks_ at the branch join and misses
 * it; the clang::CFG forward must/may lattice reports the conditional leak. */
void mutex_lock(int *p) { (void)p; }
static int m;
void f(int c) {
  if (c) mutex_lock(&m);
  return;
}
int main(void) { f(1); return 0; }
