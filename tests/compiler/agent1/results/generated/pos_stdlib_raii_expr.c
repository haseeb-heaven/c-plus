#include <stdint.h>
extern int64_t __cplus_checked_add_i64(int64_t, int64_t, unsigned, unsigned);
extern int64_t __cplus_checked_sub_i64(int64_t, int64_t, unsigned, unsigned);
extern int64_t __cplus_checked_mul_i64(int64_t, int64_t, unsigned, unsigned);
extern int64_t __cplus_checked_div_i64(int64_t, int64_t, unsigned, unsigned);
extern int64_t __cplus_checked_rem_i64(int64_t, int64_t, unsigned, unsigned);
extern int64_t __cplus_checked_shl_i64(int64_t, int64_t, unsigned, unsigned);
extern int64_t __cplus_checked_shr_i64(int64_t, int64_t, unsigned, unsigned);
extern int64_t __cplus_checked_narrow_i64(uint64_t, unsigned, unsigned, unsigned, unsigned);
#if defined(__has_builtin)
#  if __has_builtin(__builtin_add_overflow) && __has_builtin(__builtin_sub_overflow) && __has_builtin(__builtin_mul_overflow)
#    define CPLUS_HAVE_INLINE_ARITH 1
#  endif
#endif
#ifdef CPLUS_HAVE_INLINE_ARITH
#define CPLUS_ARITH_INLINE __attribute__((unused)) static inline
CPLUS_ARITH_INLINE int64_t __cplus_inline_add_i64(int64_t a, int64_t b, unsigned bits, unsigned sgn) {
  int64_t r; int32_t r32;
  if (bits == 32) {
    if (__builtin_add_overflow((int32_t)a, (int32_t)b, &r32))
      return __cplus_checked_add_i64(a, b, bits, sgn);
    return (int64_t)r32;
  }
  if (bits == 64 && sgn) {
    if (__builtin_add_overflow(a, b, &r))
      return __cplus_checked_add_i64(a, b, bits, sgn);
    return r;
  }
  return __cplus_checked_add_i64(a, b, bits, sgn);
}
CPLUS_ARITH_INLINE int64_t __cplus_inline_sub_i64(int64_t a, int64_t b, unsigned bits, unsigned sgn) {
  int64_t r; int32_t r32;
  if (bits == 32) {
    if (__builtin_sub_overflow((int32_t)a, (int32_t)b, &r32))
      return __cplus_checked_sub_i64(a, b, bits, sgn);
    return (int64_t)r32;
  }
  if (bits == 64 && sgn) {
    if (__builtin_sub_overflow(a, b, &r))
      return __cplus_checked_sub_i64(a, b, bits, sgn);
    return r;
  }
  return __cplus_checked_sub_i64(a, b, bits, sgn);
}
CPLUS_ARITH_INLINE int64_t __cplus_inline_mul_i64(int64_t a, int64_t b, unsigned bits, unsigned sgn) {
  int64_t r; int32_t r32;
  if (bits == 32) {
    if (__builtin_mul_overflow((int32_t)a, (int32_t)b, &r32))
      return __cplus_checked_mul_i64(a, b, bits, sgn);
    return (int64_t)r32;
  }
  if (bits == 64 && sgn) {
    if (__builtin_mul_overflow(a, b, &r))
      return __cplus_checked_mul_i64(a, b, bits, sgn);
    return r;
  }
  return __cplus_checked_mul_i64(a, b, bits, sgn);
}
CPLUS_ARITH_INLINE int64_t __cplus_inline_div_i64(int64_t a, int64_t b, unsigned bits, unsigned sgn) {
  if (b == 0)
    return __cplus_checked_div_i64(a, b, bits, sgn);
  if (sgn && bits > 1 && bits <= 64 && b == -1 &&
      a == (bits == 32 ? (int64_t)INT32_MIN : INT64_MIN))
    return __cplus_checked_div_i64(a, b, bits, sgn);
  return a / b;
}
CPLUS_ARITH_INLINE int64_t __cplus_inline_rem_i64(int64_t a, int64_t b, unsigned bits, unsigned sgn) {
  if (b == 0)
    return __cplus_checked_rem_i64(a, b, bits, sgn);
  if (sgn && bits > 1 && bits <= 64 && b == -1 &&
      a == (bits == 32 ? (int64_t)INT32_MIN : INT64_MIN))
    return __cplus_checked_rem_i64(a, b, bits, sgn);
  return a % b;
}
CPLUS_ARITH_INLINE int64_t __cplus_inline_shl_i64(int64_t a, int64_t b, unsigned bits, unsigned sgn) {
  if (b < 0 || (unsigned long long)b >= bits)
    return __cplus_checked_shl_i64(a, b, bits, sgn);
  if (sgn) {
    uint64_t ua = (uint64_t)a; unsigned c = (unsigned)b;
    uint64_t limit = (~(uint64_t)0) >> (64 - bits);
    if (a < 0 || ((ua << c) & ~limit))
      return __cplus_checked_shl_i64(a, b, bits, sgn);
  }
  return (int64_t)((uint64_t)a << (uint64_t)b);
}
CPLUS_ARITH_INLINE int64_t __cplus_inline_shr_i64(int64_t a, int64_t b, unsigned bits, unsigned sgn) {
  if (b < 0 || (unsigned long long)b >= bits)
    return __cplus_checked_shr_i64(a, b, bits, sgn);
  return (int64_t)(a >> (uint64_t)b);
}
CPLUS_ARITH_INLINE int64_t __cplus_inline_narrow_i64(
    uint64_t v, unsigned sb, unsigned ss, unsigned db, unsigned ds) {
  uint64_t sv = v;
  if (ss && sb > 0 && sb < 64 && ((v >> (sb - 1)) & 1u))
    sv = v | (~((uint64_t)0) << sb);
  if (db < 64) {
    uint64_t m = (((uint64_t)1) << db) - 1;
    uint64_t t = sv & m;
    if (ds && ((t >> (db - 1)) & 1u)) t |= ~m;
    if (t == sv) return (int64_t)t;
  } else if (sv == v) {
    return (int64_t)v;
  }
  return __cplus_checked_narrow_i64(v, sb, ss, db, ds);
}
#else
#define __cplus_inline_add_i64 __cplus_checked_add_i64
#define __cplus_inline_sub_i64 __cplus_checked_sub_i64
#define __cplus_inline_mul_i64 __cplus_checked_mul_i64
#define __cplus_inline_div_i64 __cplus_checked_div_i64
#define __cplus_inline_rem_i64 __cplus_checked_rem_i64
#define __cplus_inline_shl_i64 __cplus_checked_shl_i64
#define __cplus_inline_shr_i64 __cplus_checked_shr_i64
#define __cplus_inline_narrow_i64 __cplus_checked_narrow_i64
#endif
#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/stdlib_raii_expr.cp"
#include "/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/cplus-hp-98726-0.h"
#include "/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/cplus-hp-98726-1.h"
int main(void) {
  String s = String_ctor("hello");
  IntVec v = IntVec_ctor();
  for (__typeof__( 0) i = 0; i < 20; i++) IntVec_push(&v, ((int)__cplus_inline_mul_i64((int64_t)(i ), (int64_t)( i), 32, 1)));
  { __typeof__(String_len ( & s ) == 5 && String_at ( & s , 1 ) == 'e' && IntVec_len ( & v ) == 20 && IntVec_at ( & v , 19 ) == 361 ? 0 : 1) __cplus_ret = (String_len ( & s ) == 5 && String_at ( & s , 1 ) == 'e' && IntVec_len ( & v ) == 20 && IntVec_at ( & v , 19 ) == 361 ? 0 : 1); IntVec_drop(&v); String_drop(&s);  return __cplus_ret;; }
IntVec_drop(&v); String_drop(&s); 
#line 9 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/stdlib_raii_expr.cp"
}