#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define POD
#define T int
#include <ctl/priority_queue.h>
static int cmp_int(int *a, int *b){ return (*a < *b) - (*a > *b); } /* max-heap */
int main(void){
    pqu_int q = pqu_int_init(cmp_int);
    for (int i = 0; i < 10; ++i) pqu_int_push(&q, i);
    assert(q.size == 10);
    assert(*pqu_int_top(&q) == 9);
    pqu_int_pop(&q);
    assert(*pqu_int_top(&q) == 8);
    pqu_int_free(&q);
    printf("ctl_priority_queue: ok\n");
    return 0;
}
