/* Unit: memmove_s exact-fit acceptance vs too-small rejection.
 *
 * Second adversarial catalogue (bug173): an off-by-one shift is rejected rather
 * than corrupting adjacent memory.  memmove_s must accept a move whose byte
 * count exactly equals the declared destination bound, and reject any move whose
 * source count exceeds it - without ever touching memory past that bound.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "safe_memory.h"
#include "safe_error.h"
#include "safe_log.h"

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);

    /* exact fit: smax == dmax, in-place move of the whole array */
    int arr[5] = { 1, 2, 3, 4, 5 };
    assert(cplus_memmove_s(arr, sizeof arr, arr, sizeof arr) == 0);
    for (int i = 0; i < 5; ++i) assert(arr[i] == i + 1);

    /* overlapping regions are memmove's defined behaviour */
    char s[16] = "abcdef";
    assert(cplus_memmove_s(s + 1, 15, s, 6) == 0);
    assert(memcmp(s, "aabcdef", 8) == 0);

    /* too small: source count exceeds destination bound -> rejected */
    int a2[5] = { 1, 2, 3, 4, 5 };
    assert(cplus_memmove_s(a2, sizeof(int), a2, sizeof a2) != 0);
    assert(cplus_memmove_s(a2, sizeof a2 - 1, a2, sizeof a2) != 0);  /* off by one */

    /* smax > dmax rejected even when the bytes involved would fit */
    char b[8] = "abcd";
    assert(cplus_memmove_s(b, 4, b, 8) != 0);

    /* NULL destination rejected; zero-length with NULLs is a defined no-op */
    assert(cplus_memmove_s(NULL, 8, b, 4) != 0);
    assert(cplus_memmove_s(NULL, 0, NULL, 0) == 0);

    /* guard bytes past the declared bound survive both rejection and success */
    unsigned char g[16];
    memset(g, 0x5A, sizeof g);
    assert(cplus_memmove_s(g, 4, g, 8) != 0);           /* rejected */
    for (int i = 4; i < 16; ++i) assert(g[i] == 0x5A);
    memset(g, 0x5A, sizeof g);
    assert(cplus_memmove_s(g, 4, g, 4) == 0);           /* accepted, exact */
    for (int i = 4; i < 16; ++i) assert(g[i] == 0x5A);

    /* exact-fit move of one struct member into a sibling, no spill */
    struct { char a[4]; char b[4]; } rec = { "AAAA", "BBBB" };
    assert(cplus_memmove_s(rec.b, sizeof rec.b, rec.a, sizeof rec.a) == 0);
    assert(memcmp(rec.b, "AAAA", 4) == 0);

    /* a shift that grows past the bound is rejected (bug173 pattern) */
    char sh[6] = "abcde";
    assert(cplus_memmove_s(sh, sizeof sh - 1, sh, sizeof sh) != 0);

    puts("test_memmove_exact_fit: ok");
    return 0;
}
