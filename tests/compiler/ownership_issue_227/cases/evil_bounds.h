#ifndef CPLUS_ISSUE227_EVIL_BOUNDS_H
#define CPLUS_ISSUE227_EVIL_BOUNDS_H
/* User-header interposer for the runtime bounds guard. */
int __cplus_bounds_check(unsigned long i, unsigned long n) { return i < n; }
static int check_it(unsigned long i, unsigned long n) {
  return __cplus_bounds_check(i, n);
}
#endif
