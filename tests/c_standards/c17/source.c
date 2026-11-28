/* Minimal genuine C17 (ISO/IEC 9899:2018) program.
 *
 * C17 is the bug-fix edition of C11: it introduces no new language
 * facilities, but incorporates the accepted Defect Reports and clarifies
 * wording. This program uses C11 constructs that are carried into C17
 * unchanged, and deliberately exercises areas touched by C17 defect reports:
 *
 *   1. <stdalign.h> alignas / alignof and _Alignof (DR 460 alignment)
 *   2. <stdnoreturn.h> noreturn / the _Noreturn specifier
 *   3. _Static_assert (compile-time assertion, C11 -> C17)
 *   4. _Generic       (compile-time type selection, C11 -> C17)
 *   5. <stdatomic.h>  atomic_int / atomic_init / atomic_fetch_add / atomic_load
 *
 * It is a real runnable program: a conforming C17 compiler must compile it
 * under -std=c17 -pedantic-errors with no diagnostics, and the executable
 * must print exactly the single line expected in RESULTS.md.
 */
#include <stdalign.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdnoreturn.h>
#include <stdio.h>

_Static_assert(sizeof(int) >= 4, "int must be at least 32 bits");
_Static_assert(alignof(char) == 1, "char must have alignment 1");
_Static_assert(_Alignof(max_align_t) >= 8, "max_align_t alignment too small");

/* A 16-byte aligned object; alignment is a C11/C17 semantic area. */
static alignas(16) unsigned char aligned_bytes[32];

/* Both the _Noreturn specifier and the noreturn macro from <stdnoreturn.h>
 * are part of C11 and carried unchanged into C17. */
_Noreturn static void fatal_unexpected(const char *what)
{
    fprintf(stderr, "unexpected condition: %s\n", what);
    for (;;) {
        /* _Noreturn functions must not return. */
    }
}

static const char *classify(int value)
{
    return _Generic(value,
                    int: "int",
                    default: "other");
}

int main(void)
{
    atomic_int counter;

    atomic_init(&counter, 0);
    atomic_fetch_add(&counter, 7);

    const unsigned char *bytes = aligned_bytes;
    if (bytes == NULL)
        fatal_unexpected("aligned_bytes is NULL");

    printf("c17: alignof(int)=%zu type=%s counter=%d\n",
           alignof(int), classify(7), atomic_load(&counter));
    return 0;
}
