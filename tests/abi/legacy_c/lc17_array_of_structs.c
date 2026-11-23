/* lc17: arrays of records -- sorting an array of structs by a key field
 * using a comparator, then indexing the result.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

struct entry {
    int key;
    int value;
};

static int cmp_key(const void *a, const void *b)
{
    const struct entry *ea = (const struct entry *)a;
    const struct entry *eb = (const struct entry *)b;
    return (ea->key > eb->key) - (ea->key < eb->key);
}

int main(void)
{
    struct entry table[4] = {{30, 3}, {10, 1}, {40, 4}, {20, 2}};
    size_t n = sizeof(table) / sizeof(table[0]);
    int ok = 1;

    qsort(table, n, sizeof(table[0]), cmp_key);
    ok &= (table[0].key == 10 && table[0].value == 1);
    ok &= (table[1].key == 20 && table[2].key == 30);
    ok &= (table[3].key == 40 && table[3].value == 4);

    printf("lc17_array_of_structs: %s keys=%d,%d,%d,%d stride=%lu\n",
           ok ? "OK" : "FAIL",
           table[0].key, table[1].key, table[2].key, table[3].key,
           (unsigned long)sizeof(table[0]));
    return ok ? 0 : 1;
}
