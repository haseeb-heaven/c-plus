/* Unit: zero-length copies with NULL operands must be defined (no crash).
 *
 * Second adversarial catalogue (bug191): a zero-length memcpy/memmove/memset with
 * NULL pointers must not dereference anything.  The checked memory API defines
 * these as no-op success while still rejecting genuinely invalid NULL/size
 * combinations (a non-zero count, or memset_s with a NULL destination).
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

    char d[8] = "abcdefg";

    /* ---- zero-length with NULL operands: defined no-op success ---- */
    assert(cplus_memcpy_s(NULL, 0, NULL, 0) == 0);
    assert(cplus_memmove_s(NULL, 0, NULL, 0) == 0);
    assert(cplus_memcpy_s(d, sizeof d, NULL, 0) == 0);
    assert(cplus_memcpy_s(d, 0, NULL, 0) == 0);
    assert(cplus_memset_s(d, 0, 0, 0) == 0);

    /* ---- a non-zero count with a NULL operand is still a caught violation ---- */
    assert(cplus_memcpy_s(NULL, 0, NULL, 1) != 0);
    assert(cplus_memcpy_s(NULL, sizeof d, d, 1) != 0);
    assert(cplus_memcpy_s(d, sizeof d, NULL, 1) != 0);
    assert(cplus_memmove_s(d, sizeof d, NULL, 1) != 0);

    /* ---- memset_s rejects a NULL destination even for n == 0 (asymmetric) ---- */
    assert(cplus_memset_s(NULL, 0, 0, 0) != 0);
    assert(cplus_memset_s(NULL, sizeof d, 0, 0) != 0);

    /* ---- count larger than the declared destination bound is rejected ---- */
    assert(cplus_memcpy_s(d, 4, "abcd", 8) != 0);
    assert(cplus_memset_s(d, 4, 'A', 5) != 0);

    /* ---- valid operations still work and are bounded ---- */
    assert(cplus_memcpy_s(d, sizeof d, "hi", 3) == 0);
    assert(memcmp(d, "hi", 3) == 0);
    assert(cplus_memset_s(d, sizeof d, 'A', 4) == 0);
    assert(memcmp(d, "AAAA", 4) == 0);

    /* ---- guard bytes past the declared bound survive success and rejection ---- */
    unsigned char g[16];
    memset(g, 0x5A, sizeof g);
    assert(cplus_memcpy_s(g, 4, "abcdefgh", 8) != 0);   /* rejected */
    for (int i = 4; i < 16; ++i) assert(g[i] == 0x5A);
    memset(g, 0x5A, sizeof g);
    assert(cplus_memcpy_s(g, 4, "abcd", 4) == 0);       /* accepted, exact */
    for (int i = 4; i < 16; ++i) assert(g[i] == 0x5A);

    puts("test_zero_len_null_memops: ok");
    return 0;
}
