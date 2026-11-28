/* Unit: end-to-end checked allocate -> format -> grow -> free workflows. */
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

    int need = cplus_snprintf_s(NULL, 0, "%s=%d", "ans", 42);
    assert(need == 6);
    void *buf = malloc_s((size_t)need + 1);
    assert(buf != NULL);
    assert(cplus_snprintf_s((char *)buf, (size_t)need + 1, "%s=%d", "ans", 42) == need);
    assert(strcmp((char *)buf, "ans=42") == 0);
    free_s(buf);

    {
        char guard[32];
        memset(guard, 'G', sizeof guard);
        assert(cplus_snprintf_s(guard + 4, 5, "%s", "abcdefgh") == 8);
        assert(strlen(guard + 4) == 4);
        assert(guard[8] == '\0' && guard[3] == 'G' && guard[9] == 'G');
    }

    void *cz = calloc_s(16, 1);
    assert(cz != NULL);
    assert(cplus_snprintf_s((char *)cz, 16, "%d", 7) == 1);
    assert(strcmp((char *)cz, "7") == 0);
    free_s(cz);

    void *s = malloc_s(8);
    assert(s != NULL);
    assert(cplus_snprintf_s((char *)s, 8, "%s", "hi") == 2);
    s = realloc_s(s, 64);
    assert(s != NULL && strcmp((char *)s, "hi") == 0);
    assert(cplus_snprintf_s((char *)s, 64, "%s", "hi-there") == 8);
    assert(strcmp((char *)s, "hi-there") == 0);
    free_s(s);

    void *f = malloc_s(16);
    assert(f != NULL);
    assert(cplus_snprintf_s((char *)f, 16, "%s", "keepme") == 6);
    assert(realloc_s(f, (size_t)-1) == NULL);
    assert(cplus_safe_ptr_is_live(f) && strcmp((char *)f, "keepme") == 0);
    free_s(f);

    puts("test_allocfmt_roundtrip: ok");
    return 0;
}
