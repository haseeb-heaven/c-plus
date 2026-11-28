#if defined(CPLUS_AUTO_RUNTIME)
extern void cplus_set_auto_runtime(int);
static void __attribute__((constructor)) __cplus_auto_runtime_init(void) {
  cplus_set_auto_runtime(1);
}
#endif
#include <stddef.h>
extern size_t __cplus_bounds_check(size_t, size_t, const char *);
#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/core/positive/bounds_var_index_ok.c"
int main(void) { int a[8] = {0}; int i = 3; a[__cplus_bounds_check((size_t)(i), 8, "a")] = 1; return a[__cplus_bounds_check((size_t)(i), 8, "a")] == 1 ? 0 : 1; }
