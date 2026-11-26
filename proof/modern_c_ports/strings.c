/* Independent modern-C port of the "strings" the inspiration language sample workload.
 * C11, no the inspiration language source copied. See proof/modern_c_ports/README.md. */
#include "common.h"

int main(void) {
    char buf[64] = "scoped";
    CHECK("strings_len", strlen(buf) == 6);
    strcat(buf, "-c-ports");
    CHECK("strings_cat", strcmp(buf, "scoped-c-ports") == 0);
    printf("strings %s\n", buf); report("STRINGS"); return fails;
}
