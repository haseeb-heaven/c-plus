/* Independent modern-C port of the "shapes" Goose sample workload.
 * C11, no Goose source copied. See proof/modern_c_ports/README.md. */
#include "common.h"

enum kind { CIRCLE, RECT };
struct shape { enum kind k; double a, b; };
static double area(struct shape s) {
    return s.k == CIRCLE ? 3.0 * s.a * s.a : s.a * s.b;
}
int main(void) {
    struct shape c = { CIRCLE, 2.0, 0.0 }, r = { RECT, 3.0, 4.0 };
    CHECK("shapes_circle", area(c) == 12.0);
    CHECK("shapes_rect", area(r) == 12.0);
    printf("shapes %g %g\n", area(c), area(r)); report("SHAPES"); return fails;
}
