#include "cplus/cplus.h"

int main(void) {
    managed int *values = cplus_new_array(int, 8);
    auto sum = 0;

    for (auto i = 0; i < values.len; i++) {
        values[i] = i * i;
        sum += values[i];
    }

    print("MANAGED", values.len, sum);
    return 0;
}
