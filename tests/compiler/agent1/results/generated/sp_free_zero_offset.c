#include <stddef.h>
#include <stdint.h>
extern void cplus_check_ptr_formation(const void *, size_t, int64_t, const char *, int, const char *);
#include <stddef.h>
extern void *cplus_register_allocation(void *, size_t, const char *, int);
extern void cplus_free_registered(void *, const char *, int);
extern void *cplus_realloc_registered(void *, void *, size_t, const char *, int);
#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/core/positive/free_zero_offset.c"
#include <stdlib.h>
int main(void) { int *p = cplus_register_allocation((malloc(sizeof(int))), 4, __FILE__, __LINE__); if (!p) return 1; cplus_free_registered(p + 0, __FILE__, __LINE__); return 0; }