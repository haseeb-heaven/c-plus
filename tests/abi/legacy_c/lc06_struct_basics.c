/* lc06: struct basics -- aggregate initialization, pass-by-value and
 * pointer-based mutation of a mixed-field record.
 */
#include <stdio.h>

struct point {
    int x;
    int y;
    double weight;
};

static struct point translate(struct point p, int dx, int dy)
{
    p.x += dx;
    p.y += dy;
    return p;
}

static void scale_weight(struct point *p, double k)
{
    p->weight *= k;
}

int main(void)
{
    struct point a = {3, 4, 1.5};
    struct point b;
    int ok = 1;

    b = translate(a, 10, 20);
    ok &= (a.x == 3 && b.x == 13 && b.y == 24);
    scale_weight(&b, 2.0);
    ok &= (b.weight == 3.0);

    printf("lc06_struct_basics: %s a=%d,%d b=%d,%d,%.1f size=%lu\n",
           ok ? "OK" : "FAIL", a.x, a.y, b.x, b.y, b.weight,
           (unsigned long)sizeof(struct point));
    return ok ? 0 : 1;
}
