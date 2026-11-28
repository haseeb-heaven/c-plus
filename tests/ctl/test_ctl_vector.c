/* CTL vector: typed containers (upstream rurban/ctl, MIT, vendored). */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define POD
#define T int
#include <ctl/vector.h>
int main(void){
    vec_int v = vec_int_init();
    for (int i = 0; i < 20; ++i) vec_int_push_back(&v, i * i);
    assert(v.size == 20);
    assert(*vec_int_at(&v, 3) == 9 && *vec_int_at(&v, 19) == 361);
    vec_int_pop_back(&v);
    assert(v.size == 19);
    vec_int v2 = vec_int_copy(&v);
    assert(v2.size == v.size);
    vec_int_free(&v2);
    vec_int_clear(&v);
    assert(v.size == 0);
    vec_int_free(&v);
    printf("ctl_vector: ok\n");
    return 0;
}
