/* End-to-end regression for issue #212: the two reproducers go through the
 * real host sscanf path (no stubbed format/arguments), so a capacity forwarded
 * as a pointer faults and a heap overflow in format rewriting is visible to
 * ASan.  Links against the same libcplus_safecrt.a as the other CRT tests. */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "safe_format.h"
#include "safe_runtime.h"

/* Annex K capacities intentionally follow %c/%s pointers, which the host's
 * scanf format attribute does not describe. */
#if defined(__clang__)
#pragma clang diagnostic ignored "-Wformat"
#elif defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wformat"
#endif

int main(void) {
    char buf[16][2];
    char ch = 0, a = 0, b = 0, word[8];
    int val = 0;
    size_t i;

    cplus_runtime_set_violation_trap(0);

    /* Original SIGSEGV reproducer: %c's rsize_t capacity used to be consumed
     * as the %d pointer, writing through address 0x1. */
    errno = 0;
    assert(cplus_sscanf_s("a 42", "%c %d", &ch, (size_t)1, &val) == 2);
    assert(ch == 'a' && val == 42 && errno == 0);

    /* Capacity consumption for several bounded conversions in one format. */
    errno = 0;
    assert(cplus_sscanf_s("xy abc 7", "%c%c %3s %d",
                          &a, (size_t)1, &b, (size_t)1,
                          word, (size_t)sizeof(word), &val) == 4);
    assert(a == 'x' && b == 'y' && strcmp(word, "abc") == 0 && val == 7);

    /* 15 bounded conversions with capacities: the vulnerable rewrite sized
     * its buffer for 30 format bytes but needed 143. */
    memset(buf, 0, sizeof(buf));
    errno = 0;
    assert(cplus_sscanf_s("a b c d e f g h i j k l m n 42",
        "%1s%1s%1s%1s%1s%1s%1s%1s%1s%1s%1s%1s%1s%1s %d",
        buf[0], (size_t)2, buf[1], (size_t)2, buf[2], (size_t)2,
        buf[3], (size_t)2, buf[4], (size_t)2, buf[5], (size_t)2,
        buf[6], (size_t)2, buf[7], (size_t)2, buf[8], (size_t)2,
        buf[9], (size_t)2, buf[10], (size_t)2, buf[11], (size_t)2,
        buf[12], (size_t)2, buf[13], (size_t)2, &val) == 15);
    assert(errno == 0 && buf[0][0] == 'a' && buf[13][0] == 'n' && val == 42);
    for (i = 0; i < 14u; ++i) assert(buf[i][1] == '\0');

    /* Unsafe shapes are refused before any host call. */
    errno = 0;
    assert(cplus_sscanf_s("abc 42", "%s %d", word, (size_t)sizeof(word), &val) == -1);
    assert(errno == EINVAL);
    errno = 0;
    assert(cplus_sscanf_s("a 42", "%c %d", &ch, (size_t)0, &val) == -1);
    assert(errno == EINVAL);

    cplus_runtime_set_violation_trap(1);
    puts("issue212_end_to_end: ok");
    return 0;
}
