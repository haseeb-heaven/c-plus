/* Single definition of the thread-local scratch stack for the modern C+
 * DataStack allocator.  Kept dependency-free so it builds with plain clang in
 * the hardened-libc build; stdlib/datastack.hp declares the matching extern.
 * The field order/types must mirror stdlib_datastack in that header. */
#include <stddef.h>
#include <stdint.h>

typedef struct stdlib_datastack_tls {
    unsigned char *base;
    unsigned char *top;
    size_t capacity;
    size_t committed;
    uint64_t generation;
    int backing;
    size_t page;
} stdlib_datastack_tls;

__thread stdlib_datastack_tls stdlib_tls_datastack;
