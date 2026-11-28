/* End-to-end regression for issue #212's Annex K %c capacity desync.  Unlike
 * the white-box rewrite test, this one exercises the real host vsscanf path:
 * without the fix the rsize_t capacity is forwarded as the next pointer and
 * the call faults (or writes through address 0x1). */
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
    char ch = 0;
    char a = 0, b = 0, word[8];
    int val = 0;
    int n = 0;

    cplus_runtime_set_violation_trap(0);

    errno = 0;
    assert(cplus_sscanf_s("a 42", "%c %d", &ch, (size_t)1, &val) == 2);
    assert(ch == 'a' && val == 42 && errno == 0);

    memset(word, 0, sizeof(word));
    errno = 0;
    assert(cplus_sscanf_s("xy abc 7", "%c%c %s %d",
                          &a, (size_t)1, &b, (size_t)1,
                          word, (size_t)sizeof(word), &n) == 4);
    assert(a == 'x' && b == 'y');
    assert(strcmp(word, "abc") == 0 && n == 7 && errno == 0);

    cplus_runtime_set_violation_trap(1);
    puts("issue212_scanf_c_capacity: ok");
    return 0;
}
