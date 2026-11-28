/* CTL deque: typed double-ended queue (upstream rurban/ctl, MIT, vendored). */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define POD
#define T int
#include <ctl/deque.h>
int main(void){
    deq_int d = deq_int_init();
    for (int i = 0; i < 10; ++i) deq_int_push_back(&d, i);
    assert(d.size == 10);
    deq_int_push_front(&d, 100);
    assert(*deq_int_front(&d) == 100);
    assert(*deq_int_back(&d) == 9);
    deq_int_pop_front(&d);
    assert(*deq_int_front(&d) == 0);
    deq_int_free(&d);
    printf("ctl_deque: ok\n");
    return 0;
}
