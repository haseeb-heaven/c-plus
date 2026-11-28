/* Cross-TU type compatibility regression for the thread-local DataStack.
 *
 * libc/src/stdlib/datastack_tls.c defines `stdlib_tls_datastack` with the type
 * declared in datastack_tls_internal.h; stdlib/datastack.hp declares the same
 * object with its own typedef.  The two declarations must name the SAME C
 * struct tag, otherwise the cross-TU accesses are through incompatible types
 * even when the field layout happens to match.
 *
 * This test includes the libc-facing declaration and then forward-declares the
 * public tag exactly as stdlib/datastack.hp does.  __builtin_types_compatible_p
 * is 1 only when both names refer to the same struct tag, so a tag mismatch is
 * a build-time failure (negative array size), not a silent ABI change. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "../../libc/src/stdlib/datastack_tls_internal.h"

/* Tag used by stdlib/datastack.hp's `typedef struct stdlib_datastack ...`. */
struct stdlib_datastack;

typedef char assert_datastack_tls_same_tag[
    __builtin_types_compatible_p(struct stdlib_datastack, stdlib_datastack_tls)
        ? 1 : -1];

int main(void) {
    printf("DATASTACK_TLS_LAYOUT %d\n", 1);
    return 0;
}
