#include "cplus/cplus.h"
int main(void) {
    if (int x = 5; x > 3) {
        print("big", x);
    } else {
        print("small", x);
    }
    return 0;
}
