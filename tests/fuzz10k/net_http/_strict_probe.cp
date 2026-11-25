/* Minimal probe: isolate the documented http.hp strict-checker diagnostic. */
#include <curl/curl.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/http.hp"

int main(void) {
    error e = error();
    http g = http(&e);
    int v = g.valid();
    g.drop();
    return v ? 0 : 1;
}
