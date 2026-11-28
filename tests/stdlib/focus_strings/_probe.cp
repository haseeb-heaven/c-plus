#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/string.hp"
int main(void) {
    string s = string("abc");
    s.append(s.c_str());
    print("probe", s.size(), s.capacity(), s.c_str());
    return 0;
}
