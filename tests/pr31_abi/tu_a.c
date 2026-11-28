/* PR31 fixture: translation unit A -- producer / owner side.
 *
 * Compiled with plain /usr/bin/clang as C11.  Uses the shipped public headers
 * so the allocator metadata ABI is the real one; every exported pr31_a_* entry
 * point is called from a different translation unit at runtime.
 */
#define PR31_USE_CPLUS_HEADER 1
#include "pr31_shared.h"
#include "cplus_alloc_hardened.h"

#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

extern int cplus_alloc_meta_register(void *, size_t, size_t);

/* Pin the cross-TU layout in the producer as well as the consumer. */
_Static_assert(sizeof(pr31_record) == 48, "record size drift");
_Static_assert(offsetof(pr31_record, magic) == 0, "record.magic drift");
_Static_assert(offsetof(pr31_record, weight) == 16, "record.weight drift");
_Static_assert(offsetof(pr31_record, opaque) == 24, "record.opaque drift");
_Static_assert(offsetof(pr31_record, tag) == 32, "record.tag drift");
_Static_assert(sizeof(pr31_union) == 8, "union size drift");

struct pr31_handle {
    uint32_t magic;
    int secret;
    size_t bytes;
};

#define PR31_HANDLE_MAGIC 0x48314131u
static void *g_transfer_src;
static pr31_vlist_t g_vlist_cb;

void *pr31_a_malloc(size_t n) { return cplus_malloc(n); }
void *pr31_a_calloc(size_t n, size_t s) { return cplus_calloc(n, s); }
void *pr31_a_realloc(void *p, size_t n) { return cplus_realloc(p, n); }
void *pr31_a_aligned_alloc(size_t a, size_t n) { return cplus_aligned_alloc(a, n); }
char *pr31_a_strdup(const char *s) { return cplus_strdup(s); }
char *pr31_a_strndup(const char *s, size_t n) { return cplus_strndup(s, n); }

int pr31_a_release_checked(void **p) { return cplus_alloc_checked_free(p); }
int pr31_a_release_fc(void **p) { return cplus_free_checked(p); }
void pr31_a_release(void *p) { cplus_free(p); }
int pr31_a_is_live(const void *p) { return cplus_alloc_is_live(p); }

int pr31_a_register(void *p, size_t n)
{
    return cplus_alloc_meta_register(p, n, 1u) == 0 ? 1 : 0;
}

unsigned long pr31_a_invalid_frees(void) { return cplus_alloc_invalid_free_count(); }
unsigned long pr31_a_double_frees(void) { return cplus_alloc_double_free_count(); }
void pr31_a_fail_after(long n) { cplus_alloc_fail_after(n); }
void pr31_a_fail_reset(void) { cplus_alloc_fail_reset(); }
int pr31_a_atomic_realloc(void **p, size_t n) { return cplus_alloc_atomic_realloc(p, n); }

void pr31_a_set_transfer_src(void *p) { g_transfer_src = p; }
int pr31_a_transfer_to(void **dst)
{
    return cplus_transfer_ownership(dst, &g_transfer_src);
}

void pr31_a_fill_union(pr31_union *u)
{
    u->u64 = 0;
    u->halves.lo = 0x11223344u;
    u->halves.hi = 0x55667788u;
}

void pr31_a_fill_record(pr31_record *r, const pr31_union *u)
{
    size_t i;
    (void)u; /* union is filled by pr31_a_fill_union, passed separately */
    memset(r, 0, sizeof(*r));
    r->magic = PR31_RECORD_MAGIC;
    r->version = (uint16_t)PR31_RECORD_VERSION;
    r->flags = (uint16_t)PR31_RECORD_FLAGS;
    r->x = -12345;
    r->y = 67890;
    r->weight = 12.5;
    r->opaque = (void *)(uintptr_t)0xABCDEFu;
    for (i = 0; i < sizeof(r->tag); ++i)
        r->tag[i] = (char)('A' + (int)i);
    r->tag[sizeof(r->tag) - 1u] = '\0';
}

pr31_handle pr31_a_make_handle(int secret)
{
    struct pr31_handle *h = (struct pr31_handle *)cplus_malloc(sizeof(*h));
    if (h == NULL)
        return NULL;
    h->magic = PR31_HANDLE_MAGIC;
    h->secret = secret;
    h->bytes = sizeof(*h);
    return h;
}

int pr31_a_check_handle(pr31_handle h, int secret)
{
    if (h == NULL || h->magic != PR31_HANDLE_MAGIC || h->secret != secret ||
        h->bytes != sizeof(*h))
        return 0;
    return 1;
}

int pr31_a_call_cb(pr31_cb2_t cb, int x, int y)
{
    if (cb == NULL)
        return 0;
    return cb(x, y);
}

int pr31_a_vsum(int n, ...)
{
    va_list ap;
    int i, total = 0;
    va_start(ap, n);
    for (i = 0; i < n; ++i)
        total += va_arg(ap, int);
    va_end(ap);
    return total;
}

pr31_vsum_t pr31_a_vsum_ptr(void) { return pr31_a_vsum; }

void pr31_a_set_vlist_cb(pr31_vlist_t cb) { g_vlist_cb = cb; }

int pr31_a_call_vlist_cb(int n, ...)
{
    va_list ap;
    int rc;
    va_start(ap, n);
    if (g_vlist_cb == NULL) {
        va_end(ap);
        return 0;
    }
    rc = g_vlist_cb(n, ap);
    va_end(ap);
    return rc;
}

int pr31_a_get_errno(void) { return errno; }
