/* Independent modern-C port of the "strings" Goose sample workload.
 * C11, no Goose source copied. See proof/modern_c_ports/README.md. */
#include "common.h"

int main(void) {
    char buf[64] = "goose";
    CHECK("strings_len", strlen(buf) == 5);
    strcat(buf, "-c-ports");
    CHECK("strings_cat", strcmp(buf, "goose-c-ports") == 0);
    printf("strings %s\n", buf); report("STRINGS"); return fails;
}
