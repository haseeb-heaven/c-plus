/* Ordinary C APIs, unchanged spelling — now hardened by the C+ layer.
 *
 * Build:
 *   clang -std=c99 -I. -Istdlib/safe -Ilibc -Ilibc/install-include \
 *     -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include \
 *     scratch/demo.c build/safe/libcplus_safecrt.a \
 *     build/libc/libcplus_hardened.a -lm -pthread -o scratch/demo
 *
 * The include below redirects malloc/memset/free to capacity-checked wrappers.
 * No `_s` call appears in main(). The original UINT32_MAX multiplication case
 * is caught at compile time when the values are constants; here we use a
 * deliberately small allocation and an off-by-one loop to demonstrate the
 * RUNTIME path deterministically.
 */
#include "stdlib/safe/safe_c.h"
#include <stdint.h>

struct Packet {
    uint32_t count;
    uint32_t element_size;
};

int main(void) {
    struct Packet packet = {
        .count = 4,
        .element_size = 16
    };

    size_t bytes = (size_t)packet.count * packet.element_size;
    printf("Requested allocation: %zu bytes\n", bytes);

    unsigned char *buffer = malloc(bytes);
    if (!buffer)
        return 1;

    /* Intentional off-by-one: iteration `count` begins exactly at the end of
     * the tracked block. The ordinary memset must refuse it (ESNOSPC 406)
     * instead of writing past the allocation. */
    for (uint32_t i = 0; i <= packet.count; i++) {
        printf("iteration %u\n", i);
        memset(buffer + ((size_t)i * packet.element_size),
               0x41, packet.element_size);
    }

    free(buffer);
    return 0;
}
