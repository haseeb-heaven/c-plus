/* ONE caller source, compiled TWICE against the SAME saved archive:
 *   (A) plain     - no safe_c.h, ordinary host libc, links only the .a
 *   (B) safe      - force-included stdlib/safe/safe_c.h, links the .a plus the
 *                   SafeCRT/hardened support archives (caller-side only)
 *
 * For VALID inputs the two builds must print byte-identical stdout.  The only
 * thing that changes between (A) and (B) is caller-side macro expansion; the
 * precompiled C library is never rebuilt, recompiled, or relinked differently.
 */
#include "safe_layer_lib.h"

#include <stdio.h>
#include <string.h>

int main(void) {
    unsigned char bytes[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    char label[32];

    printf("app: version=%d\n", safe_layer_version());
    printf("app: mul=%d\n", safe_layer_mul(20, 22));
    printf("app: checksum=%d\n", safe_layer_checksum(bytes, 8));

    /* Ordinary C names.  In build (B) these are compile-time macro-mapped to
     * the capacity-checked SafeCRT wrappers; in (A) they are host libc. */
    strcpy(label, "precompiled");
    strcat(label, "-ok");
    printf("app: label=%s\n", label);

    printf("app: END\n");
    return 0;
}
