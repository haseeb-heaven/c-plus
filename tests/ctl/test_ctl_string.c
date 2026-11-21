/* CTL string: typed dynamic string (upstream rurban/ctl, MIT, vendored).
 * string.h is pre-instantiated (type `str`); no T macro. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctl/string.h>
int main(void){
    str s = str_init("abc");
    assert(s.size == 3);
    str_append(&s, "def");
    assert(s.size == 6);
    assert(strcmp(s.vector, "abcdef") == 0);
    str_push_back(&s, '!');
    assert(strcmp(s.vector, "abcdef!") == 0);
    str_clear(&s);
    assert(s.size == 0);
    str_free(&s);
    printf("ctl_string: ok\n");
    return 0;
}
