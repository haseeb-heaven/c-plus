#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/json.hp"
int main(void) {
    const char *path = "tests/redteam/json_net/results/deep.json";
    FILE *f = fopen(path, "w");
    if (!f) return 2;
    size_t d;
    for (d = 0; d < 20000; d++) fputc('[', f);
    for (d = 0; d < 20000; d++) fputc(']', f);
    fclose(f);
    error e = error();
    json j = json_parse_file(path, &e);
    printf("json_parse_file(20000-deep): valid=%d failed=%d\n", j.valid(), e.failed_now());
    if (j.valid()) j.drop();
    return 0;
}
