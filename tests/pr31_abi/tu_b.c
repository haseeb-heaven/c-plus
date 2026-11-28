/* PR31 fixture: translation unit B -- consumer / callback side.
 *
 * Plain C11, no cplus_libc.h.  Everything it knows about cplus comes from the
 * exported C symbols declared in pr31_shared.h.
 */
#include "pr31_shared.h"

#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>

_Static_assert(sizeof(pr31_record) == 48, "record size drift");
_Static_assert(offsetof(pr31_record, magic) == 0, "record.magic drift");
_Static_assert(offsetof(pr31_record, weight) == 16, "record.weight drift");
_Static_assert(offsetof(pr31_record, opaque) == 24, "record.opaque drift");
_Static_assert(offsetof(pr31_record, tag) == 32, "record.tag drift");
_Static_assert(sizeof(pr31_union) == 8, "union size drift");

int pr31_b_add(int a, int b) { return a + b; }

int pr31_b_vlist_sum(int n, va_list ap)
{
    int i, total = 0;
    for (i = 0; i < n; ++i)
        total += va_arg(ap, int);
    return total;
}

/* Validate field-by-field the bytes written by TU A, including a floating
 * point member, a pointer member and a trailing character array. */
int pr31_b_check_record(const pr31_record *r, const pr31_union *u)
{
    size_t i;
    if (r == NULL || u == NULL)
        return 0;
    if (r->magic != PR31_RECORD_MAGIC ||
        r->version != PR31_RECORD_VERSION ||
        r->flags != PR31_RECORD_FLAGS ||
        r->x != -12345 || r->y != 67890 ||
        r->weight != 12.5 ||
        r->opaque != (void *)(uintptr_t)0xABCDEFu)
        return 0;
    for (i = 0; i + 1u < sizeof(r->tag); ++i)
        if (r->tag[i] != (char)('A' + (int)i))
            return 0;
    if (r->tag[sizeof(r->tag) - 1u] != '\0')
        return 0;
    /* Union bitwise representation produced in the other TU. */
    if (u->halves.lo != 0x11223344u || u->halves.hi != 0x55667788u)
        return 0;
    if (u->bytes[0] != 0x44 || u->bytes[3] != 0x11 ||
        u->bytes[4] != 0x88 || u->bytes[7] != 0x55)
        return 0;
    return 1;
}

void pr31_b_set_errno(int code) { errno = code; }

/* TU B never sees the definition of struct pr31_handle; it only passes the
 * opaque pointer back to the owning TU. */
int pr31_b_use_handle(pr31_handle h, int secret)
{
    return pr31_a_check_handle(h, secret);
}

/* A block created in TU A is released here, in TU B, using only the exported
 * C symbol -- no allocator internals and no shared header macro. */
int pr31_b_release(void **p)
{
    return cplus_alloc_checked_free(p);
}
