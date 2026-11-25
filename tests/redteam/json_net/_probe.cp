#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/json.hp"

int main(void) {
    error e = error();
    char *buf = (char *)malloc(1000000);
    size_t d;
    /* nested arrays 20000 deep */
    for (d = 0; d < 20000; d++) buf[d] = '[';
    for (d = 0; d < 20000; d++) buf[20000 + d] = ']';
    buf[40000] = '\0';

    error_reset(&e);
    int w = json_input_within_budget(buf, 40000, (size_t)16384, &e);
    printf("budget_default=%d fail=%d cat=%d\n", w, e.failed_now(), e.category());

    error_reset(&e);
    json j1 = json(buf, &e);
    printf("json_ctor_valid=%d fail=%d\n", j1.valid(), e.failed_now());
    j1.drop();

    error_reset(&e);
    json j2 = json_parse_budget(buf, 40000, (size_t)25000, &e);
    printf("json_budget25k_valid=%d fail=%d\n", j2.valid(), e.failed_now());
    j2.drop();
    free(buf);
    return 0;
}
