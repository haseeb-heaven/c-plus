#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"
/* Server: bounded request queue (grow-only vector, no real network). */
static int accept_req(vector *q, int id, int cap) {
    error e = error();
    if (id < 0 || cap <= 0 || cap > 10000 || q->size() >= (size_t)cap)
        return error_fail_cat(&e, STDLIB_CATEGORY_BOUNDS, CPLUS_EXIT_RESOURCE,
                              "server: queue full/invalid"), -1;
    return q->push_back(id);
}
int main(void) {
    vector q = vector();
    int a = accept_req(&q, 1, 4);
    int b = accept_req(&q, 2, 4);
    int bad = accept_req(&q, -1, 4);
    print("SERVER_APP", a, b, q.size(), bad);
    q.drop();
    return (a == 0 && b == 0 && bad == -1) ? 0 : 1;
}
