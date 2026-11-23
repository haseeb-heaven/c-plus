#include <stddef.h>
#include <stdint.h>
extern void cplus_check_ptr_formation(const void *, size_t, int64_t, const char *, int, const char *);
#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/core/positive/pointer_one_past.c"
int main(void) { int a[4] = {0}; int *p = (cplus_check_ptr_formation((const void *)(a), (size_t)(4), (int64_t)(4), __FILE__, __LINE__, "a + 4"), (a + 4)); return p == (cplus_check_ptr_formation((const void *)(a), (size_t)(4), (int64_t)(4), __FILE__, __LINE__, "a + 4"), (a + 4)) ? 0 : 1; }