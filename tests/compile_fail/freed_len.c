#include "cplus/cplus.h"
int main(void) {
    int *p = cplus_new_array(int, 4);
    cplus_release(p);
    return (int)p.len;
}
