/*
 * Unit: cplus_strerror_s -- bounded, NUL-terminated error-message copy.
 *
 * Maps to catalogue bug139: strerror() returns a pointer into a shared static
 * buffer that the caller neither owns nor can bound; a later call (or another
 * thread) can change it.  strerror_s copies the message into a caller buffer,
 * always NUL-terminates, and refuses to write past dmax -- truncating with a
 * "..." marker when the buffer is too small, and returning ESLEMIN when even
 * the marker does not fit.
 */
#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "safe_string.h"
#include "safe_error.h"
#include "safe_log.h"

#ifndef ESNULLP
#define ESNULLP 400
#endif
#ifndef ESZEROL
#define ESZEROL 401
#endif
#ifndef ESLEMIN
#define ESLEMIN 402
#endif
#ifndef ESLEMAX
#define ESLEMAX 403
#endif

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);

    char b[64];

    /* success: a non-empty NUL-terminated message for platform errno values */
    assert(cplus_strerror_s(b, sizeof b, EINVAL) == 0);
    assert(b[0] != '\0');
    assert(strlen(b) < sizeof b);
    assert(cplus_strerror_s(b, sizeof b, ERANGE) == 0);
    assert(strlen(b) > 0);
    assert(cplus_strerror_s(b, sizeof b, ENOENT) == 0);
    assert(strlen(b) > 0);
    assert(cplus_strerror_s(b, sizeof b, EACCES) == 0);
    assert(strlen(b) > 0);

    /* safeclib constraint codes resolve through the internal message table */
    assert(cplus_strerror_s(b, sizeof b, ESNULLP) == 0);
    assert(strcmp(b, "null ptr") == 0);
    assert(cplus_strerror_s(b, sizeof b, ESZEROL) == 0);
    assert(strcmp(b, "length is zero") == 0);
    assert(cplus_strerror_s(b, sizeof b, ESLEMAX) == 0);
    assert(strcmp(b, "length exceeds RSIZE_MAX") == 0);

    /* truncation inserts a marker and still terminates */
    assert(cplus_strerror_s(b, 4, EINVAL) == 0);
    assert(strlen(b) == 3);
    assert(strcmp(b, "...") == 0);
    assert(cplus_strerror_s(b, 5, EINVAL) == 0);
    assert(strlen(b) == 4);
    assert(b[4] == '\0');
    assert(cplus_strerror_s(b, 6, EINVAL) == 0);
    assert(strlen(b) == 5);
    assert(strcmp(b + strlen(b) - 3, "...") == 0);
    assert(b[5] == '\0');

    /* dmax too small to hold even the truncation marker */
    assert(cplus_strerror_s(b, 2, EINVAL) == ESLEMIN);

    /* ---- argument / bound validation ---- */
    assert(cplus_strerror_s(NULL, sizeof b, EINVAL) == ESNULLP);
    assert(cplus_strerror_s(b, 0, EINVAL) == ESZEROL);
    assert(cplus_strerror_s(b, (rsize_t)RSIZE_MAX_STR + 1, EINVAL) == ESLEMAX);

    /* ---- guard bytes: the message copy stays inside dmax ---- */
    struct { char dst[64]; char guard[8]; } g;
    memset(g.guard, 0x5A, sizeof g.guard);
    assert(cplus_strerror_s(g.dst, sizeof g.dst, EINVAL) == 0);
    for (size_t i = 0; i < sizeof g.guard; ++i) assert(g.guard[i] == 0x5A);
    assert(cplus_strerror_s(g.dst, 4, EINVAL) == 0);
    assert(strcmp(g.dst, "...") == 0);
    for (size_t i = 0; i < sizeof g.guard; ++i) assert(g.guard[i] == 0x5A);

    puts("test_strfam_strerror_s: ok");
    return 0;
}
