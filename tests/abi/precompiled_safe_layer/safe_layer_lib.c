/* The precompiled C library.
 *
 * This file is compiled EXACTLY ONCE by /usr/bin/clang in build_lib_once.sh.
 * It is plain ISO C: no safe_c.h, no C+ headers, no C+ syntax.  Its ABI and
 * bytes must remain identical whether or not a caller opts into the checked
 * ordinary-name layer (stdlib/safe/safe_c.h).
 */
#include "safe_layer_lib.h"

int safe_layer_version(void) { return 3; }

int safe_layer_mul(int a, int b) { return a * b; }

int safe_layer_checksum(const unsigned char *data, size_t n) {
    size_t i;
    int sum = 0;
    if (!data) return -1;
    for (i = 0; i < n; ++i) sum += (int)data[i];
    return sum;
}
