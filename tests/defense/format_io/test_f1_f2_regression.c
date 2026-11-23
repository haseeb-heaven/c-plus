/* BLUE-TEAM agent 9 -- regressions for redteam findings F1 and F2.
 *
 * F1 (HIGH): the %n scanner mis-parsed repeated positional specifiers
 *   ("%1$2$n"), letting libc perform a real write (and SIGSEGV for an
 *   out-of-range index like "%11$22$n").  Positional formats are now refused
 *   before any host formatter is called.
 * F2 (MEDIUM/HIGH): __builtin_object_size(dest, 0) reported the enclosing
 *   struct size for a member, so a dmax larger than the member passed the
 *   capacity check and overwrote the adjacent field.  The macro now uses type 1
 *   (nearest enclosing object = the member itself).
 *
 * Every hostile case must be REFUSED (-1 / EINVAL) with no memory change.
 * Control cases prove literal '$', '%%' and flat arrays keep their semantics.
 */
#define _POSIX_C_SOURCE 200112L
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "safe_format.h"
#include "safe_runtime.h"

static int failures = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            ++failures;                                                        \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__,   \
                    #cond);                                                    \
        }                                                                      \
    } while (0)

#define SENTINEL 0x24681357

/* ---------------- F1: positional %n bypass ---------------- */
static void test_f1_printf(void) {
    static const char *const attacks[] = {
        "%1$2$n", "%2$1$n", "%1$2$hn", "%1$2$lln", "%01$02$n",
        "%1$-2$n", "ABCD%1$2$n", "%1$2$3$n", "%11$22$n"
    };
    volatile int sink;
    char buf[32];
    size_t i;

    cplus_runtime_set_violation_trap(0); /* refusals are -1/EINVAL */
    for (i = 0; i < sizeof attacks / sizeof attacks[0]; ++i) {
        sink = SENTINEL;
        errno = 0;
        CHECK(cplus_printf_s(attacks[i], (int *)&sink, (int *)&sink,
                             (int *)&sink, (int *)&sink, (int *)&sink) == -1);
        CHECK(errno == EINVAL);
        CHECK(sink == SENTINEL); /* no %n write, no wild-vararg access */
    }
    /* snprintf_s / sprintf_s / fprintf_s share the same scanner. */
    sink = SENTINEL;
    errno = 0;
    CHECK(cplus_snprintf_s(buf, sizeof buf, "%1$2$n", (int *)&sink) == -1);
    CHECK(errno == EINVAL && sink == SENTINEL);
    sink = SENTINEL;
    errno = 0;
    CHECK(cplus_sprintf_s(buf, sizeof buf, "%1$2$n", (int *)&sink) == -1);
    CHECK(errno == EINVAL && sink == SENTINEL);
    sink = SENTINEL;
    errno = 0;
    CHECK(cplus_fprintf_s(stderr, "%1$2$n", (int *)&sink) == -1);
    CHECK(errno == EINVAL && sink == SENTINEL);
    /* scanf family uses the companion scanner and must refuse as well. */
    sink = SENTINEL;
    errno = 0;
    CHECK(cplus_sscanf_s("42", "%1$2$n", (int *)&sink) == -1);
    CHECK(errno == EINVAL && sink == SENTINEL);

    /* Control: literal '$' and '%%' in a NON-positional format stay valid. */
    CHECK(cplus_snprintf_s(buf, sizeof buf, "price: $5") == 9);
    CHECK(strcmp(buf, "price: $5") == 0);
    CHECK(cplus_snprintf_s(buf, sizeof buf, "100%%n ok") == 8);
    CHECK(strcmp(buf, "100%n ok") == 0);
    CHECK(cplus_snprintf_s(buf, sizeof buf, "100%%%%n ok") == 9);
    CHECK(strcmp(buf, "100%%n ok") == 0);

    cplus_runtime_set_violation_trap(1);
}

/* ---------------- F2: struct-member destination capacity ---------------- */
struct S { char a[8]; char canary[8]; };

static void test_f2_struct_member(void) {
    struct S s;
    size_t i;

    cplus_runtime_set_violation_trap(0);
    memset(&s, 0x5A, sizeof s);
    errno = 0;
    /* Via the unprefixed macro: CPLUS_SAFE_OBJ_SIZE(s.a) must be 8 (type 1),
     * not 16 (type 0), so dmax=16 is refused and canary stays untouched. */
    CHECK(snprintf_s(s.a, 16, "%s", "0123456789ABCDE") == -1);
    CHECK(errno == EINVAL);
    for (i = 0; i < sizeof s.canary; ++i)
        CHECK((unsigned char)s.canary[i] == 0x5A);

    /* Control: a truthful dmax for the member still works. */
    memset(&s, 0x5A, sizeof s);
    CHECK(snprintf_s(s.a, sizeof s.a, "%s", "abc") == 3);
    CHECK(strcmp(s.a, "abc") == 0);
    for (i = 0; i < sizeof s.canary; ++i)
        CHECK((unsigned char)s.canary[i] == 0x5A);

    /* Control: flat arrays (type 0 == type 1) still get one-byte check. */
    {
        char flat[16];
        memset(flat, 'Z', sizeof flat);
        errno = 0;
        CHECK(snprintf_s(flat, sizeof flat + 1, "%s", "0123456789ABCDE") == -1);
        CHECK(errno == EINVAL);
    }
    cplus_runtime_set_violation_trap(1);
}

int main(void) {
    cplus_runtime_set_violation_trap(0);
    test_f1_printf();
    test_f2_struct_member();
    cplus_runtime_set_violation_trap(1);
    if (failures == 0) {
        puts("test_f1_f2_regression: ok");
        return 0;
    }
    printf("test_f1_f2_regression: FAILURES=%d\n", failures);
    return 1;
}
