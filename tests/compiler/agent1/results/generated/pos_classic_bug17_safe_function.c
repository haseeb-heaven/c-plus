#include <stdint.h>
extern int64_t __cplus_checked_add_i64(int64_t, int64_t, unsigned);
extern int64_t __cplus_checked_sub_i64(int64_t, int64_t, unsigned);
extern int64_t __cplus_checked_mul_i64(int64_t, int64_t, unsigned);
#if defined(__has_builtin)
#  if __has_builtin(__builtin_add_overflow) && __has_builtin(__builtin_sub_overflow) && __has_builtin(__builtin_mul_overflow)
#    define CPLUS_HAVE_INLINE_ARITH 1
#  endif
#endif
#ifdef CPLUS_HAVE_INLINE_ARITH
#define CPLUS_ARITH_INLINE __attribute__((unused)) static inline
CPLUS_ARITH_INLINE int64_t __cplus_inline_add_i64(int64_t a, int64_t b, unsigned bits) {
  int64_t r; int32_t r32;
  if (bits == 32) {
    if (__builtin_add_overflow((int32_t)a, (int32_t)b, &r32))
      return __cplus_checked_add_i64(a, b, bits);
    return (int64_t)r32;
  }
  if (bits == 64) {
    if (__builtin_add_overflow(a, b, &r))
      return __cplus_checked_add_i64(a, b, bits);
    return r;
  }
  return __cplus_checked_add_i64(a, b, bits);
}
CPLUS_ARITH_INLINE int64_t __cplus_inline_sub_i64(int64_t a, int64_t b, unsigned bits) {
  int64_t r; int32_t r32;
  if (bits == 32) {
    if (__builtin_sub_overflow((int32_t)a, (int32_t)b, &r32))
      return __cplus_checked_sub_i64(a, b, bits);
    return (int64_t)r32;
  }
  if (bits == 64) {
    if (__builtin_sub_overflow(a, b, &r))
      return __cplus_checked_sub_i64(a, b, bits);
    return r;
  }
  return __cplus_checked_sub_i64(a, b, bits);
}
CPLUS_ARITH_INLINE int64_t __cplus_inline_mul_i64(int64_t a, int64_t b, unsigned bits) {
  int64_t r; int32_t r32;
  if (bits == 32) {
    if (__builtin_mul_overflow((int32_t)a, (int32_t)b, &r32))
      return __cplus_checked_mul_i64(a, b, bits);
    return (int64_t)r32;
  }
  if (bits == 64) {
    if (__builtin_mul_overflow(a, b, &r))
      return __cplus_checked_mul_i64(a, b, bits);
    return r;
  }
  return __cplus_checked_mul_i64(a, b, bits);
}
#else
#define __cplus_inline_add_i64 __cplus_checked_add_i64
#define __cplus_inline_sub_i64 __cplus_checked_sub_i64
#define __cplus_inline_mul_i64 __cplus_checked_mul_i64
#endif
#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/classic_bug17_safe_function.c"
/* Bug 17: safe alternative to a double-evaluating macro. */
#include <stdio.h>
static int square(int x) { return ((int)__cplus_inline_mul_i64((int64_t)(x ), (int64_t)( x), 32)); }
int main(void) {
    int i = 3;
    int result = square(i++);
    printf("result=%d i=%d\n", result, i);
    return 0;
}