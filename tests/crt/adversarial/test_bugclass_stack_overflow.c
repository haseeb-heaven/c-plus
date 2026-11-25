/*
 * Bug CLASS: stack buffer overflow (raw C: char buf[N]; strcpy(buf, bigSrc)).
 *
 * The SAME bug expressed with the checked C+ bounded API: cplus_memcpy_s /
 * cplus_strcpy_s with a small local buffer (dmax == sizeof buf) and an oversized
 * source are REJECTED with a defined non-zero error. A canary placed right after
 * the buffer must stay intact (no stack smash).
 */
#include <stdio.h>
#include <string.h>
#include "safe_memory.h"
#include "safe_string.h"
#include "safe_error.h"
#include "safe_log.h"
#include "safe_lib_errno.h" /* ESNOSPC */

static int failures = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            failures++;                                                        \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__,   \
                    #cond);                                                    \
        }                                                                      \
    } while (0)

struct frame {
    unsigned char buf[8];
    unsigned char guard[8];
};

static void refill(struct frame *f) {
    memset(f, 0x5A, sizeof *f);
}

static int guard_intact(const struct frame *f) {
    for (int i = 0; i < 8; ++i)
        if (f->guard[i] != 0x5A) return 0;
    return 1;
}

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);

    struct frame f;
    unsigned char big[32];
    memset(big, 0xCC, sizeof big);

    /* memcpy into 8-byte local from 32-byte source -> rejected */
    refill(&f);
    errno_t r = cplus_memcpy_s(f.buf, sizeof f.buf, big, sizeof big);
    CHECK(r != 0);
    CHECK(r == ESNOSPC);
    CHECK(guard_intact(&f));

    /* memcpy one byte past the local buffer -> rejected, canary intact */
    refill(&f);
    CHECK(cplus_memcpy_s(f.buf, sizeof f.buf, big, sizeof f.buf + 1) != 0);
    CHECK(guard_intact(&f));

    /* exactly sizeof buf bytes is allowed and stays bounded */
    refill(&f);
    CHECK(cplus_memcpy_s(f.buf, sizeof f.buf, big, sizeof f.buf) == 0);
    CHECK(guard_intact(&f));
    for (int i = 0; i < 8; ++i) CHECK(f.buf[i] == 0xCC);

    /* strcpy of an overlong string into the local -> rejected, canary intact */
    refill(&f);
    r = cplus_strcpy_s((char *)f.buf, sizeof f.buf, "overlong_string_value");
    CHECK(r != 0);
    CHECK(r == ESNOSPC);
    CHECK(guard_intact(&f));
    CHECK(f.buf[0] == '\0'); /* failure path leaves a defined empty prefix */

    /* memmove with oversized source -> rejected, canary intact */
    refill(&f);
    CHECK(cplus_memmove_s(f.buf, sizeof f.buf, big, sizeof big) != 0);
    CHECK(guard_intact(&f));

    /* a string that fits exactly (NUL included) succeeds */
    refill(&f);
    CHECK(cplus_strcpy_s((char *)f.buf, sizeof f.buf, "abcd") == 0);
    CHECK(strcmp((char *)f.buf, "abcd") == 0);
    CHECK(guard_intact(&f));

    if (failures == 0) {
        printf("bugclass_stack_overflow: ok\n");
        return 0;
    }
    printf("bugclass_stack_overflow: FAILURES=%d\n", failures);
    return 1;
}
