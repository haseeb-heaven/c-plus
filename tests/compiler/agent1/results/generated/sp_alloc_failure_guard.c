#if defined(CPLUS_AUTO_RUNTIME)
extern void cplus_set_auto_runtime(int);
static void __attribute__((constructor)) __cplus_auto_runtime_init(void) {
  cplus_set_auto_runtime(1);
}
#endif
#include <stddef.h>
extern size_t __cplus_bounds_check(size_t, size_t, const char *);
#include <stddef.h>
extern void *cplus_register_allocation(void *, size_t, const char *, int);
extern void cplus_free_registered(void *, const char *, int);
extern void *cplus_realloc_registered(void *, void *, size_t, const char *, int);
#include <stdint.h>
#if defined(__GNUC__) || defined(__clang__)
#  define CPLUS_STATIC_INLINE __attribute__((unused)) static
#else
#  define CPLUS_STATIC_INLINE static
#endif
extern int64_t __cplus_checked_add_i64(int64_t, int64_t, unsigned, unsigned);
extern int64_t __cplus_checked_sub_i64(int64_t, int64_t, unsigned, unsigned);
extern int64_t __cplus_checked_mul_i64(int64_t, int64_t, unsigned, unsigned);
extern int64_t __cplus_checked_div_i64(int64_t, int64_t, unsigned, unsigned);
extern int64_t __cplus_checked_rem_i64(int64_t, int64_t, unsigned, unsigned);
extern int64_t __cplus_checked_shl_i64(int64_t, int64_t, unsigned, unsigned);
extern int64_t __cplus_checked_shr_i64(int64_t, int64_t, unsigned, unsigned);
extern int64_t __cplus_checked_narrow_i64(uint64_t, unsigned, unsigned, unsigned, unsigned);
#include <stddef.h>
extern size_t __cplus_checked_size_mul(size_t, size_t);
CPLUS_STATIC_INLINE size_t __cplus_inline_size_mul(size_t a, size_t b) {
  if (b != 0 && a > ((size_t)-1) / b)
    return __cplus_checked_size_mul(a, b);
  return a * b;
}
#if defined(__has_builtin)
#  if __has_builtin(__builtin_add_overflow) && __has_builtin(__builtin_sub_overflow) && __has_builtin(__builtin_mul_overflow)
#    define CPLUS_HAVE_INLINE_ARITH 1
#  endif
#endif
#ifdef CPLUS_HAVE_INLINE_ARITH
#define CPLUS_ARITH_INLINE CPLUS_STATIC_INLINE
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
#if defined(__GNUC__) || defined(__clang__)
#define CPLUS_CMPD_BIN(OP, P, T, B, BT, SG) \
  ({ __typeof__(&(P)) __cplus_ap = &(P); \
     *__cplus_ap = (T)__cplus_inline_##OP##_i64( \
         (int64_t)(*__cplus_ap), (int64_t)(B), (BT), (SG)); \
     *__cplus_ap; })
#define CPLUS_INC(OP, P, T, BT, SG, D, POST) \
  ({ __typeof__(&(P)) __cplus_ap = &(P); \
     __typeof__(*__cplus_ap) __cplus_ov = *__cplus_ap; \
     *__cplus_ap = (T)__cplus_inline_##OP##_i64( \
         (int64_t)__cplus_ov, (int64_t)(D), (BT), (SG)); \
     (POST) ? __cplus_ov : *__cplus_ap; })
#else
#define CPLUS_CMPD_BIN(OP, P, T, B, BT, SG) \
  ((P) = (T)__cplus_inline_##OP##_i64( \
      (int64_t)(P), (int64_t)(B), (BT), (SG)))
#define CPLUS_INC(OP, P, T, BT, SG, D, POST) \
  ((P) = (T)__cplus_inline_##OP##_i64((int64_t)(P), \
      (int64_t)(D), (BT), (SG)))
#endif
#define CPLUS_NEG(X, T, BT, SG) \
  ((T)__cplus_inline_sub_i64(0, (int64_t)(X), (BT), (SG)))
#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/core/positive/alloc_failure_guard.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void) {
  int *arr = (int *)cplus_register_allocation((malloc(__cplus_inline_size_mul((size_t)(sizeof(int) ), (size_t)( 5)))), 20, __FILE__, __LINE__);
  if (arr == NULL) {
    fprintf(stderr, "Allocation failed\n");
    return 1;
  }
  printf("Arr is %p\n", (void *)arr);
  int *ptr = arr;
  memset(arr, 0, sizeof(int) * 5);
  int initial[] = {10, 40, 50, 60};
  memcpy(arr, initial, sizeof(initial));
  printf("Ptr is %d\n", *ptr);
  for (int i = 0; i < 5; CPLUS_INC(add, i, int, 32, 1, 1, 1)) printf("%d ", arr[__cplus_bounds_check((size_t)(i), (size_t)(sizeof(int) * 5), "arr")]);
  printf("\n");
  cplus_free_registered(arr, __FILE__, __LINE__);
  return 0;
}
