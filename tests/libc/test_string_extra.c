#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
void *cplus_memchr(const void*,int,size_t);
char *cplus_strchr(const char*,int);
char *cplus_strrchr(const char*,int);
char *cplus_strstr(const char*,const char*);
size_t cplus_strnlen(const char*,size_t);

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
    const char *s="abcbc";
    assert(cplus_memchr(s,'c',5)==s+2);
    assert(cplus_memchr(s,'z',5)==NULL);
    assert(cplus_strchr(s,'c')==s+2);
    assert(cplus_strchr(s,'z')==NULL);
    assert(cplus_strrchr(s,'c')==s+4);
    assert(cplus_strstr(s,"bc")==s+1);
    assert(cplus_strstr(s,"zz")==NULL);
    assert(cplus_strnlen("abc",10)==3);
    assert(cplus_strnlen("abc",2)==2);

    /* --- memchr regressions (#3): NULL guard, huge n, zero, exact-fit --- */
    errno = 0;
    assert(cplus_memchr(NULL, 'x', 4) == NULL);
    assert(errno == EINVAL);
    assert(cplus_memchr(s, 'c', 0) == NULL);          /* zero length */
    assert(cplus_memchr(s, 'a', 1) == s);             /* exact-fit first byte */
    struct small_obj t;
    init_small(&t);
    errno = 0;
    assert(cplus_memchr(t.b, 'x', SIZE_MAX) == NULL); /* huge n: no OOB scan */
    assert(errno == EOVERFLOW);
    assert(canary_intact(&t));

    puts("string_extra: ok");
    return 0;
}
