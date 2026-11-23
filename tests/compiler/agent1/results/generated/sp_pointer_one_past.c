#include <stddef.h>
#include <stdint.h>
extern void cplus_check_ptr_formation(const void *, size_t, int64_t, const char *, int, const char *);
extern void cplus_check_bounds(const void *, size_t, size_t, const char *, int, const char *);
#if defined(__GNUC__) || defined(__clang__)
#define CPLUS_PTR_BODY(B, I, C, N, T, D) \
  __typeof__(&(B)) __cplus_bp = &(B); \
  __typeof__(I) __cplus_ip = (I); \
  if (D) cplus_check_bounds((const void *)(*__cplus_bp), \
      (size_t)(C), (size_t)(__cplus_ip), __FILE__, __LINE__, T); \
  else cplus_check_ptr_formation((const void *)(*__cplus_bp), \
      (size_t)(C), (int64_t)(__cplus_ip) * ((N) ? -1 : 1), \
      __FILE__, __LINE__, T);
#define CPLUS_PTR_CHECK(B, I, C, N, T, D) \
  ({ CPLUS_PTR_BODY(B, I, C, N, T, D) \
     ((N) ? (*__cplus_bp - __cplus_ip) \
          : (*__cplus_bp + __cplus_ip)); })
#define CPLUS_PTR_CHECK_ASSIGN(B, I, C, N, T, D) \
  ({ CPLUS_PTR_BODY(B, I, C, N, T, D) \
     *__cplus_bp = ((N) ? (*__cplus_bp - __cplus_ip) \
                       : (*__cplus_bp + __cplus_ip)); \
     *__cplus_bp; })
#else
#define CPLUS_PTR_CHECK(B, I, C, N, T, D) \
  ((D) ? (cplus_check_bounds((const void *)(B), (size_t)(C), \
       (size_t)(I), __FILE__, __LINE__, T), (B) + (I)) \
       : (cplus_check_ptr_formation((const void *)(B), \
       (size_t)(C), (int64_t)(I) * ((N) ? -1 : 1), \
       __FILE__, __LINE__, T), (B) + (I)))
#define CPLUS_PTR_CHECK_ASSIGN(B, I, C, N, T, D) \
  ((B) = CPLUS_PTR_CHECK(B, I, C, N, T, D))
#endif
#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/core/positive/pointer_one_past.c"
int main(void) { int a[4] = {0}; int *p = CPLUS_PTR_CHECK(a ,  4, 4, 0, "a + 4", 0); return p == CPLUS_PTR_CHECK(a ,  4, 4, 0, "a + 4", 0) ? 0 : 1; }
