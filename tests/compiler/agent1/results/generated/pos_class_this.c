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
#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/class_this.cp"
#include "cplus/cplus.h"

struct Box {
int value;
};
typedef struct Box Box;
void Box_drop(Box *self);
#line 6 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/class_this.cp"
Box Box_ctor(int v) {
Box _cplus_self = {0};
#line 6 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/class_this.cp"
{ _cplus_self.value = v; }
return _cplus_self;
Box_drop(&_cplus_self); 
#line 15 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/class_this.cp"
}
#line 8 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/class_this.cp"
void Box_add(Box *self, int n)
#line 8 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/class_this.cp"
{ self->value += n; }
#line 10 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/class_this.cp"
int Box_get(Box *self)
#line 10 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/class_this.cp"
{ return self->value; }
#line 12 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/class_this.cp"
int Box_doubled(Box *self)
#line 12 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/class_this.cp"
{ return ((int)__cplus_inline_mul_i64((int64_t)(Box_get(self) ), (int64_t)( 2), 32)); }
#line 14 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/class_this.cp"
void Box_drop(Box *self)
#line 14 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/class_this.cp"
{ print("drop", self->value); }
#line 17 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/class_this.cp"


int main(void) {
    Box b = Box_ctor(10);
    Box_add(&b, 5);
    print("v", Box_get(&b), "d", Box_doubled(&b));
    { __typeof__(0) __cplus_ret = (0); Box_drop(&b);  return __cplus_ret;; }
Box_drop(&b); 
#line 40 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/class_this.cp"
}