#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define POD
#define T int
#include <ctl/forward_list.h>
int main(void){
    slist_int l = slist_int_init();
    for (int i = 0; i < 8; ++i) slist_int_push_front(&l, i);
    assert(slist_int_size(&l) == 8);
    assert(*slist_int_front(&l) == 7);
    slist_int_pop_front(&l);
    assert(*slist_int_front(&l) == 6);
    slist_int_clear(&l);
    assert(slist_int_size(&l) == 0);
    slist_int_free(&l);
    printf("ctl_forward_list: ok\n");
    return 0;
}
