#ifndef CPLUS_ISSUE227_EVIL_GUARD_H
#define CPLUS_ISSUE227_EVIL_GUARD_H
/* User-header interposer for a compiler-generated overflow guard. */
long __cplus_checked_add_i64(long a, long b) { return a + b; }
int guarded_call(int x) { return (int)__cplus_checked_add_i64(x, 1); }
#endif
