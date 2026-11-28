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
#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/core/positive/safe_memory.c"
#include <stdio.h>
#include "cplus/cplus.h"
int main(void) {
  char buf[16];
  int ok = cplus_strcpy(buf, sizeof(buf), "core") == 4;
  ok = ok && cplus_strnlen(buf, sizeof(buf)) == 4;
  ok = ok && cplus_memcpy(CPLUS_PTR_CHECK(buf ,  1, 16, 0, "buf + 1", 0), buf, 4) == CPLUS_PTR_CHECK(buf ,  1, 16, 0, "buf + 1", 0);
  ok = ok && buf[0] == 'c' && buf[1] == 'c' && buf[2] == 'o';
  ok = ok && cplus_memcpy(NULL, NULL, 0) == NULL;
  return ok ? 0 : 1;
}
