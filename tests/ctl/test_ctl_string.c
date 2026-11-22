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

    /* Defensive NULL & bounds guards added by Agent 2 */
    str null_s = str_init(NULL);
    assert(null_s.size == 0);
    str_append(&null_s, NULL);
    str_append(NULL, "ignored");
    assert(str_find(&null_s, "x") == SIZE_MAX);
    assert(str_find(&null_s, NULL) == SIZE_MAX);
    assert(str_rfind(&null_s, "x") == SIZE_MAX);
    assert(str_count(&null_s, 'x') == 0);
    str_free(&null_s);

    printf("ctl_string: ok\n");
    return 0;
}
