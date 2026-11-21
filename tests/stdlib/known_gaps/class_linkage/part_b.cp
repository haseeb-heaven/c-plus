/* Second TU of the duplicate-symbol reproduction. */
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/string.hp"

int gap_b(void) {
    String s = String("bb");
    return (int)s.len();
}
