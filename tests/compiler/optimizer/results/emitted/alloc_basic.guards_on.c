#include <stddef.h>
extern void *cplus_register_allocation(void *, size_t, const char *, int);
extern void cplus_free_registered(void *, const char *, int);
extern void *cplus_realloc_registered(void *, void *, size_t, const char *, int);
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
#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/compiler/optimizer/cases/alloc_basic.c"
#include <stdio.h>
#include <stdlib.h>
int main(void){
  int i;
  int *p;
  p = cplus_register_allocation((malloc(4*sizeof(int))), 16, __FILE__, __LINE__);
  if(!p) return 2;
  for(i=0;i<4;i++) p[i]=((int)__cplus_inline_add_i64((int64_t)(i), (int64_t)(1), 32, 1));
  printf("ab=%d,%d\n", p[0], p[3]);
  cplus_free_registered(p, __FILE__, __LINE__);
  printf("ab=clean\n");
  return 0;
}