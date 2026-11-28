#if defined(CPLUS_AUTO_RUNTIME)
extern void cplus_set_auto_runtime(int);
static void __attribute__((constructor)) __cplus_auto_runtime_init(void) {
  cplus_set_auto_runtime(1);
}
#endif
#include <stddef.h>
extern void *cplus_alloc_array(size_t, size_t);
extern void cplus_release(void *);
extern size_t cplus_alloc_bytes(const void *);
extern size_t __cplus_bounds_check(size_t, size_t, const char *);
#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/core/positive/classic_bug53_vla_stack.c"
/* Bug 53: unbounded VLA / alloca stack allocation. */
#include <stdio.h>
void f(size_t n) { size_t __cplus_vla_count_0_buf = (size_t)(n); int  *buf = (int *)cplus_alloc_array(__cplus_vla_count_0_buf, sizeof(int )); buf[__cplus_bounds_check((size_t)(0), (size_t)(__cplus_vla_count_0_buf), "buf")]=1; printf("%d\n",buf[__cplus_bounds_check((size_t)(0), (size_t)(__cplus_vla_count_0_buf), "buf")]); cplus_release((void *)buf); }
int main(void) { f(100); return 0; }
