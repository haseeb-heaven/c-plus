/* Legacy 01 -- record packing (malloc / memset / memcpy / strcpy / printf).
 *
 * Typical of 1990s C: a heap record is zeroed, filled, then bitwise-copied
 * into a stack value of the same type. No _s names, no annotations.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int  id;
    int  year;
    char tag[16];
} record_t;

int main(void)
{
    record_t *r = (record_t *)malloc(sizeof(record_t));
    record_t  copy;

    if (!r) return 2;
    memset(r, 0, sizeof(*r));
    r->id = 42;
    r->year = 1998;
    strcpy(r->tag, "ALPHA");

    memcpy(&copy, r, sizeof(copy));
    printf("record_pack: id=%d year=%d tag=%s\n", copy.id, copy.year, copy.tag);

    free(r);
    return 0;
}
