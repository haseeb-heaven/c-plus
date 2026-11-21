#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define POD
#define T int
#include <ctl/stack.h>
int main(void){
    stack_int s = stack_int_init();
    for (int i = 0; i < 10; ++i) stack_int_push(&s, i);
    assert(s.size == 10);
    assert(*stack_int_top(&s) == 9);
    stack_int_pop(&s);
    assert(*stack_int_top(&s) == 8);
    stack_int_free(&s);
    printf("ctl_stack: ok\n");
    return 0;
}
