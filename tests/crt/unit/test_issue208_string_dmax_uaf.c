/* Regression for issue #208: direct string _s entry points must prove dmax
 * (safeclib clears dmax bytes on some refusal paths), bound read-side scans,
 * check delim/save-pointer UAF in strtok_s, and publish errno. */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "safe_alloc.h"
#include "safe_error.h"
#include "safe_runtime.h"
#include "safe_string.h"

#define ESNOSPC_C 406

int main(void) {
    char *dest, *src, *delim;
    size_t dmax;
    int diff = 0;
    char *save;

    cplus_runtime_set_violation_trap(0);
    dest = (char *)malloc_s(8);
    src = (char *)malloc_s(8);
    delim = (char *)malloc_s(4);
    assert(dest != NULL && src != NULL && delim != NULL);
    memset(dest, 0x5a, 8u);
    strcpy(src, "abc");
    strcpy(delim, " ,");

    /* An oversized dmax on a tracked 8-byte destination is refused BEFORE
     * safeclib can zero 256 bytes into the heap buffer. */
    errno = 0;
    assert(cplus_strcpy_s(dest, 256u, src) == ESNOSPC_C && errno == ERANGE);
    assert((unsigned char)dest[0] == 0x5a && (unsigned char)dest[7] == 0x5a);
    errno = 0;
    assert(cplus_strncpy_s(dest, 256u, src, 3u) == ESNOSPC_C && errno == ERANGE);
    assert((unsigned char)dest[0] == 0x5a && (unsigned char)dest[7] == 0x5a);
    errno = 0;
    assert(cplus_strcat_s(dest, 256u, src) == ESNOSPC_C && errno == ERANGE);
    assert((unsigned char)dest[0] == 0x5a && (unsigned char)dest[7] == 0x5a);
    errno = 0;
    assert(cplus_strncat_s(dest, 256u, src, 3u) == ESNOSPC_C && errno == ERANGE);
    assert((unsigned char)dest[0] == 0x5a && (unsigned char)dest[7] == 0x5a);
    errno = 0;
    assert(cplus_strerror_s(dest, 256u, EINVAL) == ESNOSPC_C && errno == ERANGE);
    assert((unsigned char)dest[0] == 0x5a && (unsigned char)dest[7] == 0x5a);

    /* Read-side bounds: no scan past a proven short source. */
    errno = 0;
    assert(cplus_strcmp_s(dest, 64u, src, &diff) == ESNOSPC_C && errno == ERANGE);
    assert(diff == 0);
    errno = 0;
    assert(cplus_strnlen_s(src, 64u) == 0u && errno == ERANGE);

    /* strtok_s checks UAF on delim and on the caller's save pointer. */
    free_s(delim);
    strcpy(dest, "a b");
    dmax = 8u;
    save = NULL;
    assert(cplus_strtok_s(dest, &dmax, delim, &save) == NULL);
    delim = (char *)malloc_s(4);
    assert(delim != NULL);
    strcpy(delim, " ");
    save = (char *)malloc_s(8);
    assert(save != NULL);
    free_s(save);
    assert(cplus_strtok_s(dest, &dmax, delim, &save) == NULL);

    /* Refusals from safeclib publish a standard errno on direct entry too. */
    errno = 0;
    assert(cplus_strcpy_s(NULL, 8u, src) != 0 && errno == EINVAL);

    free_s(delim);
    free_s(src);
    free_s(dest);
    puts("issue208_string_dmax_uaf: ok");
    return 0;
}
