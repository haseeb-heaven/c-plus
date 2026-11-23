/*
 * lc30 - opaque handles: incomplete type exposed, definition hidden in TU.
 *
 * Independent legacy-C ABI probe. Clients only ever see `struct lc30_handle *`;
 * layout and storage live behind create/access/destroy functions.
 */
#include <stdio.h>
#include <stdlib.h>

struct lc30_handle; /* opaque to callers */

struct lc30_handle {
    unsigned int magic;
    int id;
    int payload[6];
};

#define LC30_MAGIC 0x1C30u

struct lc30_handle *lc30_create(int id)
{
    struct lc30_handle *h = (struct lc30_handle *)malloc(sizeof(*h));
    int i;

    if (h == NULL) {
        return NULL;
    }
    h->magic = LC30_MAGIC;
    h->id = id;
    for (i = 0; i < 6; ++i) {
        h->payload[i] = id + i;
    }
    return h;
}

int lc30_valid(const struct lc30_handle *h)
{
    return h != NULL && h->magic == LC30_MAGIC;
}

int lc30_sum(const struct lc30_handle *h)
{
    int total = 0;
    int i;
    if (!lc30_valid(h)) {
        return -1;
    }
    for (i = 0; i < 6; ++i) {
        total += h->payload[i];
    }
    return total;
}

int lc30_id(const struct lc30_handle *h)
{
    return lc30_valid(h) ? h->id : -1;
}

void lc30_destroy(struct lc30_handle *h)
{
    if (lc30_valid(h)) {
        h->magic = 0u;
        free(h);
    }
}

int main(void)
{
    struct lc30_handle *h = lc30_create(30);
    int sum, id, valid_before;

    if (h == NULL) {
        printf("lc30 result: FAIL allocation\n");
        return 1;
    }
    sum = lc30_sum(h);
    id = lc30_id(h);
    valid_before = lc30_valid(h);
    lc30_destroy(h);
    /* Do not dereference h after destroy: the storage has been released. */
    if (id != 30 || sum != (30 + 31 + 32 + 33 + 34 + 35) || !valid_before) {
        printf("lc30 result: FAIL id=%d sum=%d\n", id, sum);
        return 1;
    }
    printf("lc30 result: OK id=%d sum=%d destroyed\n", id, sum);
    return 0;
}
