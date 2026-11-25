#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"
#include "stdlib/collections.hp"
/* Database engine: bounded table with duplicate-key refusal and sum query. */
static int insert(intmap *idx, vector *vals, int key, int val) {
    error e = error();
    int dummy = 0;
    if (key < 0 || key > 100000 || val < 0 || val > 1000000)
        return error_fail_cat(&e, STDLIB_CATEGORY_RANGE, CPLUS_EXIT_BOUNDS,
                              "db: value out of range"), -1;
    if (idx->contains(key))
        return error_fail_cat(&e, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_OWNERSHIP,
                              "db: duplicate key"), -1;
    if (vals->push_back(val) != 0) return -1;
    if (idx->put(key, val) != 0) return -1;
    (void)dummy;
    return 0;
}
int main(void) {
    intmap idx = intmap();
    vector vals = vector();
    int a = insert(&idx, &vals, 1, 10);
    int b = insert(&idx, &vals, 2, 20);
    int dup = insert(&idx, &vals, 1, 99);
    int bad = insert(&idx, &vals, -5, 10);
    int sum = 0; size_t i;
    for (i = 0; i < vals.size(); i++) sum += vals.at(i);
    print("DATABASE_ENGINE", a, b, dup, bad, sum);
    vals.drop();
    return (a == 0 && b == 0 && dup == -1 && bad == -1 && sum == 30) ? 0 : 1;
}
