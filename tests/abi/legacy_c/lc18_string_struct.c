/* lc18: structs with inline character arrays -- a fixed-size string field,
 * length tracking and safe copies between records.
 */
#include <stdio.h>
#include <string.h>

#define NAME_CAP 16

struct person {
    char name[NAME_CAP];
    unsigned int age;
};

static void person_set(struct person *p, const char *n, unsigned int a)
{
    size_t len = strlen(n);
    if (len >= NAME_CAP) {
        len = NAME_CAP - 1;
    }
    memcpy(p->name, n, len);
    p->name[len] = '\0';
    p->age = a;
}

int main(void)
{
    struct person p;
    struct person q;
    int ok = 1;

    person_set(&p, "Ada", 36);
    q = p;
    ok &= (strcmp(p.name, "Ada") == 0);
    ok &= (q.age == 36 && strcmp(q.name, p.name) == 0);
    person_set(&q, "Grace", 45);
    ok &= (strcmp(q.name, "Grace") == 0);
    ok &= (strcmp(p.name, "Ada") == 0);

    printf("lc18_string_struct: %s p=%s/%u q=%s/%u cap=%lu\n",
           ok ? "OK" : "FAIL", p.name, p.age, q.name, q.age,
           (unsigned long)NAME_CAP);
    return ok ? 0 : 1;
}
