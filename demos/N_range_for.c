/* Demo N: range-based for over a fixed array and a managed allocation.
 *
 * `for (auto v : arr)` lowers to a counted loop using arr.len, and `v` is
 * declared with __typeof__ of the element. */
#include "cplus/cplus.h"

class Player {
    int score;
};

int main(void) {
    int scores[] = {10, 20, 30};
    auto total = 0;
    for (auto value : scores) {
        total += value;
    }
    print("total", total);

    managed int *buffer = cplus_new_array(int, 3);
    for (auto i : buffer) {
        (void)i;
    }
    print("buffer.len", buffer.len);
    return 0;
}
