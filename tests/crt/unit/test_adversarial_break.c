/* Adversarial tests: attempt to "break" checked APIs with invalid/boundary inputs.
 * All cases must be defined behavior for the checked API (error return) or normal. */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include "safe_memory.h"
#include "safe_string.h"
#include "safe_alloc.h"
#include "safe_io.h"
#include "safe_format.h"
#include "safe_error.h"

int main(void) {
    char d[8]; char s[8] = "abc";
    /* memcpy_s: NULLs, zero, overflow, exact boundary */
    assert(cplus_memcpy_s(NULL, 8, s, 3) != 0);
    assert(cplus_memcpy_s(d, 8, NULL, 3) != 0);
    assert(cplus_memcpy_s(d, 0, s, 1) != 0);
    assert(cplus_memcpy_s(d, 2, s, 3) != 0); /* too small */
    (void)cplus_memcpy_s(d, 8, s, 0);

    /* memmove_s overlap and invalid */
    char o[8] = "abcd";
    assert(cplus_memmove_s(o + 1, 7, o, 3) == 0);
    assert(cplus_memmove_s(o, 2, o, 4) != 0);

    /* memset_s */
    assert(cplus_memset_s(NULL, 8, 0, 1) != 0);
    { unsigned char b4 = (unsigned char)d[4]; (void)cplus_memset_s(d, 4, 1, 5); assert((unsigned char)d[4] == b4); }

    /* string */
    assert(cplus_strcpy_s(d, 8, "ok") == 0);
    assert(cplus_strcpy_s(d, 2, "toolong") != 0);
    assert(cplus_strcat_s(d, 8, "!") == 0);
    assert(cplus_strncat_s(d, 8, "xxxx", 1) == 0);
    int ind = 0; assert(cplus_strcmp_s("a", 2, "b", &ind) == 0 && ind < 0);
    assert(cplus_strnlen_s(NULL, 5) == 0);
    assert(cplus_strnlen_s("abc", 2) == 2);

    /* alloc */
    void *p = malloc_s(16);
    assert(p != NULL);
    errno = 0;
    assert(realloc_s(p, (size_t)-1) == NULL && errno == EINVAL);
    assert(cplus_safe_ptr_is_live(p));
    free_s(p);
    free_s(p); /* double free safely refused */
    errno = 0;
    assert(calloc_s((size_t)-1, 2) == NULL && errno == EOVERFLOW);

    /* io */
    FILE *f = cplus_fopen_s("/tmp/cplus_adv.tmp", "wb+");
    assert(f != NULL);
    assert(cplus_fwrite_s("abc", 3, 1, 3, f) == 3);
    rewind(f); char rb[4] = {0}; assert(cplus_fread_s(rb, 4, 1, 3, f) == 3); assert(strcmp(rb, "abc") == 0);
    assert(cplus_fclose_s(f) == 0);
    assert(cplus_fclose_s(f) == EOF);
    remove("/tmp/cplus_adv.tmp");

    /* format */
    char fb[8]; assert(cplus_snprintf_s(fb, sizeof(fb), "%s", "hi") == 2);
    assert(cplus_sprintf_s(fb, sizeof(fb), "%d", 1) == 1);
    puts("adversarial_break: ok");
    return 0;
}
