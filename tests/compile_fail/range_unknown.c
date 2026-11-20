#include "cplus/cplus.h"

int main(void) {
    int *unknown;
    for (auto v : unknown) {
        (void)v;
    }
    return 0;
}
