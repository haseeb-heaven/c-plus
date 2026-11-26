/* Independent modern-C port of the "records" the inspiration language sample workload.
 * C11, no the inspiration language source copied. See proof/modern_c_ports/README.md. */
#include "common.h"

struct rec { int id; const char *name; };
int main(void) {
    struct rec r[3] = {{3, "c"}, {1, "scoped"}, {2, "port"}};
    int i;
    /* insertion sort by id */
    for (i = 1; i < 3; ++i) {
        struct rec t = r[i]; int j = i - 1;
        while (j >= 0 && r[j].id > t.id) { r[j + 1] = r[j]; j--; }
        r[j + 1] = t;
    }
    for (i = 0; i < 3; ++i) printf("records %d %s\n", r[i].id, r[i].name);
    CHECK("records_sorted", r[0].id == 1 && r[2].id == 3);
    report("RECORDS"); return fails;
}
