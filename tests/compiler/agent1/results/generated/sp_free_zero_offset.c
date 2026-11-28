#if defined(CPLUS_AUTO_RUNTIME)
extern void cplus_set_auto_runtime(int);
static void __attribute__((constructor)) __cplus_auto_runtime_init(void) {
  cplus_set_auto_runtime(1);
}
#endif
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
#include <stddef.h>
extern void *cplus_register_allocation(void *, size_t, const char *, int);
extern void cplus_free_registered(void *, const char *, int);
extern void *cplus_realloc_registered(void *, void *, size_t, const char *, int);
#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/core/positive/free_zero_offset.c"
#include <stdlib.h>
int main(void) { int *p = cplus_register_allocation((malloc(sizeof(int))), 4, __FILE__, __LINE__); if (!p) return 1; cplus_free_registered(p + 0, __FILE__, __LINE__); return 0; }
