/* Flexible array members (GNU extension supported by the gnu89 backend). */
#ifndef CPLUS_LEGACY_C
#error "compatibility fixture must be built in LEGACY C mode"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Msg { int len; char data[]; };

int main(void) {
    struct Msg *m = (struct Msg *)malloc(sizeof(struct Msg) + 6);
    m->len = 5;
    strcpy(m->data, "hello");
    printf("%d %s\n", m->len, m->data);
    printf("%d\n", (int)(sizeof(struct Msg) == sizeof(int)));
    free(m);
    return 0;
}
