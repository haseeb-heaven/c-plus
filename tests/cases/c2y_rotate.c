#include "cplus/cplus.h"
int main(void) {
    print(cplus_rotl32(1u, 4), cplus_rotr32(0x10u, 4));
    return 0;
}
