#include "cplus/cplus.h"

int main(void) {
    int values[] = {10, 20, 30};
    int total = 0;
    for (auto v : values) {
        total += v;
    }
    print("total", total);

    managed int *heap = cplus_new_array(int, 3);
    int i = 0;
    for (auto x : heap) {
        heap[i] = i + 1;
        (void)x;
        i++;
    }
    print("heap", heap[0], heap[1], heap[2], "len", heap.len);
    return 0;
}
