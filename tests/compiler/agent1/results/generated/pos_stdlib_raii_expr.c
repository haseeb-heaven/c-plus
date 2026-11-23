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
#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/stdlib_raii_expr.cp"
#include "/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/cplus-hp-82154-0.h"
#include "/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/cplus-hp-82154-1.h"
int main(void) {
  String s = String_ctor("hello");
  IntVec v = IntVec_ctor();
  for (__typeof__( 0) i = 0; i < 20; i++) IntVec_push(&v, ((int)__cplus_inline_mul_i64((int64_t)(i ), (int64_t)( i), 32)));
  { __typeof__(String_len ( & s ) == 5 && String_at ( & s , 1 ) == 'e' && IntVec_len ( & v ) == 20 && IntVec_at ( & v , 19 ) == 361 ? 0 : 1) __cplus_ret = (String_len ( & s ) == 5 && String_at ( & s , 1 ) == 'e' && IntVec_len ( & v ) == 20 && IntVec_at ( & v , 19 ) == 361 ? 0 : 1); IntVec_drop(&v); String_drop(&s);  return __cplus_ret;; }
IntVec_drop(&v); String_drop(&s); 
#line 9 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/stdlib_raii_expr.cp"
}