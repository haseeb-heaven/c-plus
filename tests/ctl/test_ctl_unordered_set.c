/* CTL unordered_set (upstream rurban/ctl, MIT, vendored). */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define POD
#define T int
#include <ctl/unordered_set.h>
int main(void){
    uset_int s = uset_int_init(0, 0);
    for (int i = 0; i < 20; ++i) uset_int_insert(&s, i);
    uset_int_insert(&s, 5);              /* duplicate */
    assert(s.size == 20);
    assert(uset_int_contains(&s, 3));
    uset_int_erase(&s, 3);
    assert(!uset_int_contains(&s, 3));
    assert(s.size == 19);
    uset_int_clear(&s);
    assert(s.size == 0);
    uset_int_free(&s);
    printf("ctl_unordered_set: ok\n");
    return 0;
}
