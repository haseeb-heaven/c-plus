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
#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/class_ctor_dtor.c"
#include "cplus/cplus.h"

struct Point {
int x;
int y;
};
typedef struct Point Point;
void Point_drop(Point *self);
#line 7 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/class_ctor_dtor.c"
Point Point_ctor(int a , int b) {
Point _cplus_self = {0};
#line 7 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/class_ctor_dtor.c"
{
        _cplus_self.x = a;
        _cplus_self.y = b;
    }
return _cplus_self;
Point_drop(&_cplus_self); 
#line 19 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/class_ctor_dtor.c"
}
#line 12 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/class_ctor_dtor.c"
int Point_sum(Point *self)
#line 12 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/class_ctor_dtor.c"
{
        return ((int)__cplus_inline_add_i64((int64_t)(self->x ), (int64_t)( self->y), 32));
    }
#line 16 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/class_ctor_dtor.c"
void Point_drop(Point *self)
#line 16 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/class_ctor_dtor.c"
{
        print("drop", Point_sum(self));
    }
#line 21 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/class_ctor_dtor.c"


int main(void) {
    Point p = Point_ctor(2, 3);
    print("sum=", Point_sum(&p));
    { __typeof__(0) __cplus_ret = (0); Point_drop(&p);  return __cplus_ret;; }
Point_drop(&p); 
#line 39 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/class_ctor_dtor.c"
}