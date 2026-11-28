#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
/* Web app: bounded route matching, no sockets (deterministic core). */
static int route(const char *path) {
    error e = error();
    if (path == 0 || strlen(path) > 128)
        return error_fail_cat(&e, STDLIB_CATEGORY_BOUNDS, CPLUS_EXIT_BOUNDS,
                              "web: bad path"), -1;
    if (strcmp(path, "/") == 0) return 200;
    if (strcmp(path, "/health") == 0) return 204;
    return 404;
}
int main(void) {
    print("WEB_APP", route("/"), route("/health"), route("/x"), route(""));
    return (route("/") == 200 && route("/health") == 204 &&
            route("/x") == 404 && route("") == 404) ? 0 : 1;
}
