#include <stdio.h>
#include "cplus/cplus.h"

int main(void) {
    int *arr;
    {
        size_t size = 3;
        arr = (int *)cplus_alloc(sizeof(int) * size);
        for (int i = 0; i < 3; i++)
            arr[i] = (i + 1) * 10;
    }
    /* `size` is out of scope here: .len must use the runtime query. */
    for (size_t idx = 0; idx < arr.len; idx++)
        printf("%d\t", arr[idx]);
    printf("\n");
    cplus_release(arr);
    return 0;
}
