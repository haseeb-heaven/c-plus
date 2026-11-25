#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define POD
#define T int
#include <ctl/queue.h>
int main(void){
    queue_int q = queue_int_init();
    for (int i = 0; i < 10; ++i) queue_int_push(&q, i);
    assert(q.size == 10);
    assert(*queue_int_front(&q) == 0);
    queue_int_pop(&q);
    assert(*queue_int_front(&q) == 1);
    queue_int_free(&q);
    printf("ctl_queue: ok\n");
    return 0;
}
