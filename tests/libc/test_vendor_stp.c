/* Standalone C99 test for the vendored safeclib stpcpy_s / stpncpy_s group.
 *
 * Build (from the repo root):
 *   /usr/bin/clang -O2 -std=c99 -D__STDC_WANT_LIB_EXT1__=0 \
 *     -Ithird_party/safeclib/include -Ibuild/safe \
 *     -Ithird_party/safeclib/src -Istdlib/safe -Wno-macro-redefined \
 *     -c libc/src/safecrt_vendor/stp_group.c -o /tmp/stp_group.o
 *   /usr/bin/clang -O2 -std=c99 -Ilibc/install-include \
 *     tests/libc/test_vendor_stp.c /tmp/stp_group.o \
 *     build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
 *     -o /tmp/test_vendor_stp
 *   /tmp/test_vendor_stp
 *
 * Prints "vendor_stp: <checks> <failures>" and exits nonzero on any failure.
 */
#include <stdio.h>
#include <string.h>

#include "cplus_vendor_stp.h"

#define BOS_UNKNOWN ((size_t)-1)

/* Silence the safeclib default constraint handler so stdout carries only the
 * required summary line. This symbol comes from libcplus_safecrt.a. */
typedef void (*cplus_v_stp_handler_t)(const char *, void *, int);
cplus_v_stp_handler_t cplus_sc_set_str_constraint_handler_s(
    cplus_v_stp_handler_t handler);

static void quiet_handler(const char *msg, void *ptr, int err)
{
    (void)msg;
    (void)ptr;
    (void)err;
}

static int checks = 0;
static int failures = 0;

static void check(int ok, const char *what)
{
    ++checks;
    if (!ok) {
        ++failures;
        printf("vendor_stp: FAIL %s\n", what);
    }
}

int main(void)
{
    char dest[32];
    char *r;
    int err;

    (void)cplus_sc_set_str_constraint_handler_s(quiet_handler);

    /* ---- 1. stpcpy_s: valid copy + pointer-to-terminator semantics ---- */
    memset(dest, 'X', sizeof(dest));
    err = -1;
    r = cplus_v__stpcpy_s_chk(dest, sizeof(dest), "hello", &err, sizeof(dest),
                              BOS_UNKNOWN);
    check(r == dest + 5, "stpcpy_s return points at terminator");
    check(err == CPLUS_V_STP_EOK, "stpcpy_s EOK");
    check(strcmp(dest, "hello") == 0, "stpcpy_s copied string");

    /* ---- 2. stpncpy_s: valid copy + pointer-to-terminator semantics ---- */
    memset(dest, 'X', sizeof(dest));
    err = -1;
    r = cplus_v__stpncpy_s_chk(dest, sizeof(dest), "world", 5, &err,
                               sizeof(dest), BOS_UNKNOWN);
    check(r == dest + 5, "stpncpy_s return points at terminator");
    check(err == CPLUS_V_STP_EOK, "stpncpy_s EOK");
    check(strcmp(dest, "world") == 0, "stpncpy_s copied string");

    /* ---- 3. stpncpy_s: truncation by slen still NUL-terminates ---- */
    memset(dest, 'X', sizeof(dest));
    err = -1;
    r = cplus_v__stpncpy_s_chk(dest, sizeof(dest), "abcdef", 3, &err,
                               sizeof(dest), BOS_UNKNOWN);
    check(r == dest + 3, "stpncpy_s slen-limited return points at terminator");
    check(err == CPLUS_V_STP_EOK, "stpncpy_s slen-limited EOK");
    check(strcmp(dest, "abc") == 0, "stpncpy_s slen-limited copy");

    /* ---- 4. NULL out-parameter / pointers ---- */
    memset(dest, 'X', sizeof(dest));
    r = cplus_v__stpcpy_s_chk(dest, sizeof(dest), "hello", NULL, sizeof(dest),
                              BOS_UNKNOWN);
    check(r == NULL, "stpcpy_s NULL errp returns NULL");

    err = -1;
    r = cplus_v__stpcpy_s_chk(NULL, 8, "hello", &err, 8, BOS_UNKNOWN);
    check(r == NULL && err == CPLUS_V_STP_ESNULLP,
          "stpcpy_s NULL dest -> ESNULLP");

    memset(dest, 'X', sizeof(dest));
    err = -1;
    r = cplus_v__stpcpy_s_chk(dest, sizeof(dest), NULL, &err, sizeof(dest),
                              BOS_UNKNOWN);
    check(r == NULL && err == CPLUS_V_STP_ESNULLP,
          "stpcpy_s NULL src -> ESNULLP");
    check(dest[0] == '\0', "stpcpy_s NULL src nulls dest");

    r = cplus_v__stpncpy_s_chk(dest, sizeof(dest), "hello", 5, NULL,
                               sizeof(dest), BOS_UNKNOWN);
    check(r == NULL, "stpncpy_s NULL errp returns NULL");

    err = -1;
    r = cplus_v__stpncpy_s_chk(NULL, 8, "hello", 5, &err, 8, BOS_UNKNOWN);
    check(r == NULL && err == CPLUS_V_STP_ESNULLP,
          "stpncpy_s NULL dest -> ESNULLP");

    memset(dest, 'X', sizeof(dest));
    err = -1;
    r = cplus_v__stpncpy_s_chk(dest, sizeof(dest), NULL, 5, &err, sizeof(dest),
                               BOS_UNKNOWN);
    check(r == NULL && err == CPLUS_V_STP_ESNULLP,
          "stpncpy_s NULL src -> ESNULLP");
    check(dest[0] == '\0', "stpncpy_s NULL src nulls dest");

    /* ---- 5. zero bounds ---- */
    memset(dest, 'X', sizeof(dest));
    err = -1;
    r = cplus_v__stpcpy_s_chk(dest, 0, "hello", &err, sizeof(dest), BOS_UNKNOWN);
    check(r == NULL && err == CPLUS_V_STP_ESZEROL,
          "stpcpy_s dmax 0 -> ESZEROL");

    err = -1;
    r = cplus_v__stpncpy_s_chk(dest, 0, "hello", 5, &err, sizeof(dest),
                               BOS_UNKNOWN);
    check(r == NULL && err == CPLUS_V_STP_ESZEROL,
          "stpncpy_s dmax 0 -> ESZEROL");

    memset(dest, 'X', sizeof(dest));
    err = -1;
    r = cplus_v__stpncpy_s_chk(dest, sizeof(dest), "hello", 0, &err,
                               sizeof(dest), BOS_UNKNOWN);
    check(r == dest && err == CPLUS_V_STP_EOK && dest[0] == '\0',
          "stpncpy_s slen 0 yields empty string");

    /* ---- 6. one-over bounds (dmax > real object size) ---- */
    {
        char small[8];
        memset(small, 'X', sizeof(small));
        err = -1;
        r = cplus_v__stpcpy_s_chk(small, sizeof(small) + 1, "abc", &err,
                                  sizeof(small), BOS_UNKNOWN);
        check(r == NULL && err == CPLUS_V_STP_EOVERFLOW,
              "stpcpy_s dmax one over destbos -> EOVERFLOW");
        check(small[0] == '\0', "stpcpy_s overflow nulls dest");

        memset(small, 'X', sizeof(small));
        err = -1;
        r = cplus_v__stpncpy_s_chk(small, sizeof(small) + 1, "abc", 3, &err,
                                   sizeof(small), BOS_UNKNOWN);
        check(r == NULL && err == CPLUS_V_STP_EOVERFLOW,
              "stpncpy_s dmax one over destbos -> EOVERFLOW");
        check(small[0] == '\0', "stpncpy_s overflow nulls dest");
    }

    /* ---- 7. truncation (dmax too small for src + NUL) ---- */
    {
        char tiny[4];
        memset(tiny, 'X', sizeof(tiny));
        err = -1;
        r = cplus_v__stpcpy_s_chk(tiny, sizeof(tiny), "abcdef", &err,
                                  sizeof(tiny), BOS_UNKNOWN);
        check(r == NULL && err == CPLUS_V_STP_ESNOSPC,
              "stpcpy_s truncation -> ESNOSPC");
        check(tiny[0] == '\0', "stpcpy_s truncation nulls dest");

        memset(tiny, 'X', sizeof(tiny));
        err = -1;
        r = cplus_v__stpncpy_s_chk(tiny, sizeof(tiny), "abcdef", 6, &err,
                                   sizeof(tiny), BOS_UNKNOWN);
        check(r == NULL && err == CPLUS_V_STP_ESNOSPC,
              "stpncpy_s truncation -> ESNOSPC");
        check(tiny[0] == '\0', "stpncpy_s truncation nulls dest");
    }

    /* ---- 8. over-max lengths with unknown object size ---- */
    err = -1;
    r = cplus_v__stpcpy_s_chk(dest, CPLUS_V_STP_RSIZE_MAX_STR + 1, "abc", &err,
                              BOS_UNKNOWN, BOS_UNKNOWN);
    check(r == NULL && err == CPLUS_V_STP_ESLEMAX,
          "stpcpy_s dmax > RSIZE_MAX_STR -> ESLEMAX");

    memset(dest, 'X', sizeof(dest));
    err = -1;
    r = cplus_v__stpncpy_s_chk(dest, sizeof(dest), "abc",
                               CPLUS_V_STP_RSIZE_MAX_STR + 1, &err,
                               BOS_UNKNOWN, BOS_UNKNOWN);
    check(r == NULL && err == CPLUS_V_STP_ESLEMAX,
          "stpncpy_s slen > RSIZE_MAX_STR -> ESLEMAX");

    printf("vendor_stp: %d %d\n", checks, failures);
    return failures != 0;
}
