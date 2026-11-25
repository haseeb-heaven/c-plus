/* CTL set: typed ordered set (upstream rurban/ctl, MIT, vendored). */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define POD
#define T int
#include <ctl/set.h>
static int cmp_int(int *a, int *b) { return (*a > *b) - (*a < *b); }
int main(void){
    set_int s = set_int_init(cmp_int);
    for (int i = 0; i < 10; ++i) set_int_insert(&s, i);
    set_int_insert(&s, 5);          /* duplicate ignored */
    assert(s.size == 10);
    assert(set_int_contains(&s, 3));
    set_int_erase(&s, 3);
    assert(!set_int_contains(&s, 3));
    assert(s.size == 9);
    set_int_clear(&s);
    assert(s.size == 0);
    set_int_free(&s);
    printf("ctl_set: ok\n");
    return 0;
}
