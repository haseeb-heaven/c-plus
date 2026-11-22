#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
void *cplus_memrchr(const void*,int,size_t);
int cplus_strcasecmp(const char*,const char*);
int cplus_strncasecmp(const char*,const char*,size_t);
char *cplus_strdup(const char*);
char *cplus_strncpy(char*,const char*,size_t);
char *cplus_strncpy_bounded(char*,const char*,size_t,size_t);
char *cplus_stpncpy(char*,const char*,size_t);

/* Canary directly after an 8-byte object: any OOB write from a huge-n
 * strncpy/stpncpy is visible here. Used by the UBSan/ASan regression runs. */
struct small_obj { char b[8]; unsigned char can[8]; };
static void init_small(struct small_obj *t) {
    memset(t, 0xCD, sizeof(*t));
    memset(t->can, 0xA5, sizeof(t->can));
}
static int canary_intact(const struct small_obj *t) {
    for (size_t i = 0; i < sizeof(t->can); ++i) if (t->can[i] != 0xA5u) return 0;
    return 1;
}

int main(void){
    const char *s="ababa"; assert(cplus_memrchr(s,'a',5)==s+4);
    assert(cplus_strcasecmp("AbC","aBc")==0);
    assert(cplus_strncasecmp("abcd","abZZ",2)==0);
    char *d=cplus_strdup("hi"); assert(d && strcmp(d,"hi")==0); free(d);

    /* --- memrchr regressions (#3): NULL guard, huge n, zero, exact-fit --- */
    errno = 0;
    assert(cplus_memrchr(NULL, 'x', 4) == NULL);
    assert(errno == EINVAL);
    const char *e = "abc";
    assert(cplus_memrchr(e, 'c', 0) == NULL);       /* zero length */
    assert(cplus_memrchr(e, 'c', 3) == e + 2);       /* exact-fit last byte */
    struct small_obj t;
    init_small(&t);
    errno = 0;
    assert(cplus_memrchr(t.b, 'x', SIZE_MAX) == NULL); /* huge n: no OOB scan */
    assert(errno == EOVERFLOW);
    assert(canary_intact(&t));

    /* --- strncpy regressions (#2): huge n, zero, exact-fit, canary ------- */
    init_small(&t);
    errno = 0;
    assert(cplus_strncpy(t.b, "abc", SIZE_MAX) == t.b);
    assert(errno == EOVERFLOW);
    assert(canary_intact(&t));                       /* no zero-fill overflow */
    init_small(&t);
    assert(cplus_strncpy(t.b, "abc", 0) == t.b);     /* zero length: unchanged */
    assert(canary_intact(&t));
    init_small(&t);
    assert(cplus_strncpy(t.b, "abc", 3) == t.b);     /* exact fit */
    assert(memcmp(t.b, "abc", 3) == 0 && canary_intact(&t));

    /* --- strncpy_bounded regressions (#2): known/unknown cap ----------- */
    init_small(&t);
    errno = 0;
    assert(cplus_strncpy_bounded(t.b, "abc", SIZE_MAX, sizeof(t.b)) == NULL);
    assert(errno == ERANGE);                         /* known cap -> refusal */
    assert(canary_intact(&t));
    init_small(&t);
    errno = 0;
    assert(cplus_strncpy_bounded(t.b, "abc", SIZE_MAX, (size_t)-1) == NULL);
    assert(errno == EOVERFLOW);                      /* unknown cap -> clamp */
    assert(canary_intact(&t));
    init_small(&t);
    assert(cplus_strncpy_bounded(t.b, "abc", sizeof(t.b), sizeof(t.b)) == t.b);
    assert(memcmp(t.b, "abc", 3) == 0 && canary_intact(&t)); /* exact-fit cap */

    /* --- stpncpy regressions (#3): huge n never reaches tail memset ---- */
    init_small(&t);
    errno = 0;
    assert(cplus_stpncpy(t.b, "abc", SIZE_MAX) == NULL);
    assert(errno == EOVERFLOW);
    assert(canary_intact(&t));
    init_small(&t);
    errno = 0;
    assert(cplus_stpncpy(NULL, "abc", 4) == NULL);
    assert(errno == EINVAL);
    init_small(&t);
    assert(cplus_stpncpy(t.b, "abc", 8) == t.b + 3);  /* zero-fill to n */
    assert(t.b[3] == '\0' && canary_intact(&t));

    puts("string_extra2: ok"); return 0;
}
