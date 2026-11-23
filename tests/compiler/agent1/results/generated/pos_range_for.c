#include <stddef.h>
extern size_t __cplus_bounds_check(size_t, size_t, const char *);
#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/range_for.c"
#include "cplus/cplus.h"

int main(void) {
    int values[] = {10, 20, 30};
    int total = 0;
    for (size_t __cplus_range_i0 = 0; __cplus_range_i0 < (3); __cplus_range_i0++) { __typeof__(values[__cplus_bounds_check((size_t)(__cplus_range_i0), 3, "values")]) v = values[__cplus_bounds_check((size_t)(__cplus_range_i0), 3, "values")]; {
        total += v;
    }}
    print("total", total);

     int *heap = cplus_new_array(int, 3); 
    int i = 0;
    for (size_t __cplus_range_i1 = 0; __cplus_range_i1 < ((3)); __cplus_range_i1++) { __typeof__(heap[__cplus_range_i1]) x = heap[__cplus_range_i1]; {
        heap[i] = i + 1;
        (void)x;
        i++;
    }}
    print("heap", heap[0], heap[1], heap[2], "len", ((3)));
    { __typeof__(0) __cplus_ret = (0); cplus_release(heap); heap = NULL;  return __cplus_ret;; }
cplus_release(heap); heap = NULL; 
#line 21 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/range_for.c"
}