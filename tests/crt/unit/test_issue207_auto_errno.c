/* White-box regression for issue #207's second half: the *_s_auto wrappers
 * must not overwrite the ERANGE published by the checked primitive with a
 * blanket EINVAL.  The safeclib entry points are stubbed to return ESNOSPC
 * (406) so the error path is exercised deterministically. */
#define CPLUS_SAFE_DIAG_NO_MACROS
#define CPLUS_SAFE_NO_UNPREFIXED_MACROS
#include "../../../stdlib/safe/src/safe_memory.c"
#include <assert.h>
#include <errno.h>
#include <stdio.h>

void cplus_safe_log(cplus_safe_log_level level, const char *api, const char *msg) {
    (void)level; (void)api; (void)msg;
}
void cplus_safe_stat_record(const char *api, int ok) { (void)api; (void)ok; }
void cplus_runtime_violation(const char *api, const char *detail) { (void)api; (void)detail; }
int cplus_safe_ptr_was_freed(const void *ptr) { (void)ptr; return 0; }

static size_t g_fake_capacity = 64u;
size_t cplus_safe_remaining_capacity(const void *ptr) {
    (void)ptr;
    return g_fake_capacity;
}

errno_t cplus_sc_memcpy_s(void *dest, rsize_t dmax, const void *src, rsize_t smax) {
    (void)dest; (void)dmax; (void)src; (void)smax; return 406;
}
errno_t cplus_sc_memmove_s(void *dest, rsize_t dmax, const void *src, rsize_t smax) {
    (void)dest; (void)dmax; (void)src; (void)smax; return 406;
}
errno_t cplus_sc_memset_s(void *dest, rsize_t dmax, int value, rsize_t n) {
    (void)dest; (void)dmax; (void)value; (void)n; return 406;
}
errno_t cplus_sc_memcmp_s(const void *dest, rsize_t dmax, const void *src,
                          rsize_t smax, int *diff) {
    (void)dest; (void)dmax; (void)src; (void)smax;
    if (diff) *diff = 0;
    return 406;
}
errno_t cplus_sc_memzero_s(void *dest, rsize_t dmax) {
    (void)dest; (void)dmax; return 406;
}

int main(void) {
    char dst[64];
    char src[64];
    int diff = 0;

    memset(dst, 0x5a, sizeof(dst));
    memset(src, 'A', sizeof(src));

    errno = 0;
    assert(cplus_memcpy_s_auto(dst, src, 8u, (size_t)-1) == NULL);
    assert(errno == ERANGE);
    errno = 0;
    assert(cplus_memmove_s_auto(dst, src, 8u, (size_t)-1) == NULL);
    assert(errno == ERANGE);
    errno = 0;
    assert(cplus_memset_s_auto(dst, 0, 8u, (size_t)-1) == NULL);
    assert(errno == ERANGE);
    errno = 0;
    assert(cplus_memcmp_s_auto(dst, src, 8u, (size_t)-1, (size_t)-1) == -1);
    assert(errno == ERANGE);

    puts("issue207_auto_errno: ok");
    return 0;
}
