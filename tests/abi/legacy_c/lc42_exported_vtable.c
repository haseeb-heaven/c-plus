/*
 * lc42 - shared-library boundary simulation (part 2): object-style ABI.
 *
 * Exported factory returns a struct whose members are function pointers
 * (a C vtable). The caller knows only the struct declaration, not how the
 * implementation stores its state; this mirrors a plugin boundary.
 */
#include <stdio.h>
#include <stdlib.h>

/* Public ABI types (declaration only). */
struct lc42_vtable;

struct lc42_instance {
    const struct lc42_vtable *vt;
    int value;
};

struct lc42_vtable {
    int (*get)(const struct lc42_instance *self);
    void (*set)(struct lc42_instance *self, int value);
    int (*bump)(struct lc42_instance *self, int delta);
    void (*destroy)(struct lc42_instance *self);
};

/* Exported implementation functions (external linkage). */
int lc42_get(const struct lc42_instance *self)
{
    return self->value;
}

void lc42_set(struct lc42_instance *self, int value)
{
    self->value = value;
}

int lc42_bump(struct lc42_instance *self, int delta)
{
    self->value += delta;
    return self->value;
}

void lc42_destroy(struct lc42_instance *self)
{
    free(self);
}

static const struct lc42_vtable lc42_vtable_impl = {
    lc42_get, lc42_set, lc42_bump, lc42_destroy
};

struct lc42_instance *lc42_create(int initial)
{
    struct lc42_instance *obj =
        (struct lc42_instance *)malloc(sizeof(*obj));
    if (obj == NULL) {
        return NULL;
    }
    obj->vt = &lc42_vtable_impl;
    obj->value = initial;
    return obj;
}

int main(void)
{
    struct lc42_instance *obj = lc42_create(40);
    int v1, v2, v3;

    if (obj == NULL) {
        printf("lc42 result: FAIL allocation\n");
        return 1;
    }
    obj->vt->set(obj, 41);
    v1 = obj->vt->get(obj);
    v2 = obj->vt->bump(obj, 1);
    v3 = obj->vt->bump(obj, -2);
    obj->vt->destroy(obj);
    if (v1 != 41 || v2 != 42 || v3 != 40) {
        printf("lc42 result: FAIL v1=%d v2=%d v3=%d\n", v1, v2, v3);
        return 1;
    }
    printf("lc42 result: OK v1=%d v2=%d v3=%d\n", v1, v2, v3);
    return 0;
}
