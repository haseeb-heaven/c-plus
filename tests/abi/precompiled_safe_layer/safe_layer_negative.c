/* Negative control: the checked ordinary-name layer (force-included
 * stdlib/safe/safe_c.h) must REFUSE an overflow of a local buffer, while the
 * UNCHANGED precompiled C library is still called normally in the same process.
 *
 * Expected behaviour:
 *   - stdout proves the saved library was called (version/mul) and that a
 *     valid in-bounds memset succeeds;
 *   - the 64-byte memset into char[8] is macro-mapped to cplus_memset_s_auto,
 *     which sees __builtin_object_size(small,1)==8, refuses the write, reports
 *     "[CPLUS-SAFE] memset_s: buffer overflow refused ... ESNOSPC 406" on
 *     stderr, and (default violation trap) terminates with exit code 70;
 *   - the trailing "UNEXPECTED" line must NOT be printed.
 */
#include "safe_layer_lib.h"

#include <stdio.h>
#include <string.h>

int main(void) {
    char small[8];

    /* The saved, unchanged precompiled library is called as usual. */
    printf("negative: lib_version=%d lib_mul=%d\n",
           safe_layer_version(), safe_layer_mul(7, 6));

    /* A valid, in-bounds write through the checked ordinary name succeeds. */
    memset(small, 'A', 8);
    printf("negative: valid_write=ok small0=%c\n", small[0]);
    fflush(stdout);

    /* 64 bytes into an 8-byte local buffer: must be refused, never performed. */
    memset(small, 'B', 64);

    /* Reaching here means the overflow was NOT refused. */
    printf("negative: UNEXPECTED overflow was not refused\n");
    return 1;
}
