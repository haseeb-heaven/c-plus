/* CTL map: typed ordered map (upstream rurban/ctl, MIT, vendored). */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
typedef struct { int key; int val; } kv;
static void kv_free(kv *self) { (void)self; }
static kv   kv_copy(kv *self) { return *self; }
#define T kv
#include <ctl/map.h>
static int cmp_kv(kv *a, kv *b) { return (a->key > b->key) - (a->key < b->key); }
int main(void){
    map_kv m = map_kv_init(cmp_kv);
    for (int i = 0; i < 5; ++i) { kv e = { i, i * 10 }; map_kv_insert(&m, e); }
    assert(m.size == 5);
    kv key = { 3, 0 };
    map_kv_it it = map_kv_find(&m, key);
    assert(it.ref && it.ref->val == 30);
    map_kv_erase(&m, key);
    assert(m.size == 4);
    map_kv_free(&m);
    printf("ctl_map: ok\n");
    return 0;
}
