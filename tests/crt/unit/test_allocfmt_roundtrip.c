/* Unit: end-to-end checked allocate -> format -> grow -> free workflows.
 *
 * Catalogue mapping:
 *   bug02/bug33 - checked allocation feeds formatting without uninit reads.
 *   bug32       - failed growth preserves already-formatted contents.
 *   bug81/bug144- the two-pass "measure then write" pattern is exact.
 *   bug143      - guard bytes bound the write when the target is tight.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include "safe_alloc.h"
#include "safe_format.h"
#include "safe_error.h"
#include "safe_log.h"

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);

    /* ---- two-pass: measure with the dmax==0 query, then write exactly ---- */
    int need = cplus_snprintf_s(NULL, 0, "%s=%d", "ans", 42);   /* "ans=42" */
    assert(need == 6);
    void *buf = NULL;
    assert(malloc_s(&buf, (size_t)need + 1) == CPLUS_SAFE_OK);
    assert(buf != NULL);
    assert(cplus_snprintf_s((char *)buf, (size_t)need + 1, "%s=%d", "ans", 42) == need);
    assert(strcmp((char *)buf, "ans=42") == 0);
    free_s(&buf);
    assert(buf == NULL);

    /* ---- tight output: writing into a buffer one byte too small is safe ---- */
    {
        char guard[32];
        memset(guard, 'G', sizeof guard);
        assert(cplus_snprintf_s(guard + 4, 5, "%s", "abcdefgh") == 8);
        assert(strlen(guard + 4) == 4);
        assert(guard[8] == '\0');           /* NUL at dest + dmax - 1 */
        assert(guard[3] == 'G');            /* before dest untouched */
        assert(guard[9] == 'G');            /* one past dmax untouched */
    }

    /* ---- calloc-backed buffer formats into pre-zeroed storage (bug33) ---- */
    void *cz = NULL;
    assert(calloc_s(&cz, 16, 1) == CPLUS_SAFE_OK);
    assert(cz != NULL);
    assert(cplus_snprintf_s((char *)cz, 16, "%d", 7) == 1);
    assert(strcmp((char *)cz, "7") == 0);
    free_s(&cz);
    assert(cz == NULL);

    /* ---- growth preserves formatted contents ---- */
    void *s = NULL;
    assert(malloc_s(&s, 8) == CPLUS_SAFE_OK);
    assert(cplus_snprintf_s((char *)s, 8, "%s", "hi") == 2);
    assert(strcmp((char *)s, "hi") == 0);
    assert(realloc_s(&s, 64) == CPLUS_SAFE_OK);
    assert(s != NULL);
    assert(strcmp((char *)s, "hi") == 0);       /* survived the move */
    assert(cplus_snprintf_s((char *)s, 64, "%s", "hi-there") == 8);
    assert(strcmp((char *)s, "hi-there") == 0);
    free_s(&s);
    assert(s == NULL);

    /* ---- failed growth after a format keeps pointer + bytes (bug32) ---- */
    void *f = NULL;
    assert(malloc_s(&f, 16) == CPLUS_SAFE_OK);
    assert(cplus_snprintf_s((char *)f, 16, "%s", "keepme") == 6);
    void *old = f;
    assert(realloc_s(&f, (size_t)-1) == CPLUS_SAFE_ENOMEM);
    assert(f == old);
    assert(strcmp((char *)f, "keepme") == 0);
    free_s(&f);
    assert(f == NULL);

    puts("test_allocfmt_roundtrip: ok");
    return 0;
}
