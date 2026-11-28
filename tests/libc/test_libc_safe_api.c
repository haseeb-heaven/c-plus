/* Ordinary C APIs are safe with ONLY the plain libc layer linked.
 * No SafeCRT, no `_s` spelling in the test bodies. */
#include "cplus_libc_safe.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>

int main(void) {
    unsigned char *p;
    char *s;
    size_t before, after;

    setvbuf(stdout, NULL, _IONBF, 0);

    /* In-bounds ordinary calls still behave exactly as C. */
    p = (unsigned char *)malloc(64);
    assert(p != NULL);
    memset(p, 0x41, 64);
    assert(p[0] == 0x41 && p[63] == 0x41);

    /* Interior pointer with insufficient remaining capacity is refused. */
    errno = 0;
    memset(p + 48, 0x42, 32);
    assert(errno == ERANGE);
    assert(p[48] == 0x41);

    /* One-past-the-end has KNOWN zero capacity: the original crash case. */
    errno = 0;
    memset(p + 64, 0x43, 16);
    assert(errno == ERANGE);

    /* Ordinary memcpy and strcpy are bounded too. */
    errno = 0;
    memcpy(p + 60, "abcdefgh", 8);
    assert(errno == ERANGE);

    s = (char *)malloc(4);
    assert(s != NULL);
    errno = 0;
    strcpy(s, "abcdef");
    assert(errno == ERANGE);

    /* Strict mode refuses genuinely unknown capacity; default only warns. */
    cplus_libc_set_unknown_capacity_mode(1);
    errno = 0;
    {
        char stack[8];
        memset(stack, 0x44, sizeof stack); /* stack size is known -> allowed */
        assert(stack[7] == 0x44);
    }
    cplus_libc_set_unknown_capacity_mode(0);

    /* Logging never changes results or allocations. */
    before = cplus_alloc_live_count();
    cplus_libc_log_set_enabled(1);
    memset(p, 0x45, 16);
    cplus_libc_log_set_enabled(0);
    after = cplus_alloc_live_count();
    assert(before == after);

    free(s);
    free(p);
    assert(cplus_alloc_live_count() == 0);
    puts("libc_safe_api: ok");
    return 0;
}
