#include "cplus/cplus.h"

int main(void) {
    int *value = cplus_new(int);
    *value = 17;
    cplus_release(value);
    cplus_release(value); /* Must trap: CPLUS_EXIT_DOUBLE_FREE == 104. */
    return 0;
}
