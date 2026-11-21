/* CTL list: typed doubly linked list (upstream rurban/ctl, MIT, vendored). */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define POD
#define T int
#include <ctl/list.h>
int main(void){
    list_int l = list_int_init();
    for (int i = 0; i < 8; ++i) list_int_push_back(&l, i);
    list_int_push_front(&l, 99);
    assert(l.size == 9);
    assert(*list_int_front(&l) == 99);
    assert(*list_int_back(&l) == 7);
    list_int_pop_front(&l);
    assert(*list_int_front(&l) == 0);
    list_int_clear(&l);
    assert(l.size == 0);
    list_int_free(&l);
    printf("ctl_list: ok\n");
    return 0;
}
